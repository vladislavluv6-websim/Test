<?php

declare(strict_types=1);

namespace PrivateBlocks;

use pocketmine\block\Block;
use pocketmine\block\VanillaBlocks;
use pocketmine\command\Command;
use pocketmine\command\CommandSender;
use pocketmine\event\block\BlockBreakEvent;
use pocketmine\event\block\BlockPlaceEvent;
use pocketmine\event\entity\EntityExplodeEvent;
use pocketmine\event\Listener;
use pocketmine\event\player\PlayerInteractEvent;
use pocketmine\event\player\PlayerJoinEvent;
use pocketmine\event\player\PlayerMoveEvent;
use pocketmine\math\Vector3;
use pocketmine\player\Player;
use pocketmine\plugin\PluginBase;
use pocketmine\utils\Config;
use pocketmine\utils\TextFormat as TF;
use pocketmine\world\particle\FloatingTextParticle;
use pocketmine\world\sound\XpLevelUpSound;
use pocketmine\world\World;

final class BlockPrivates extends PluginBase implements Listener{
    private const QUESTION_MARK = TF::LIGHT_PURPLE . "{?}" . TF::WHITE;

    /** @var array<string, array<string, mixed>> */
    private array $regions = [];

    /** @var array<string, string> */
    private array $insideRegionByPlayer = [];

    private Config $storage;

    protected function onEnable() : void{
        @mkdir($this->getDataFolder());
        $this->storage = new Config($this->getDataFolder() . "regions.yml", Config::YAML, []);
        $loaded = $this->storage->getAll();
        $this->regions = is_array($loaded) ? $loaded : [];

        $this->getServer()->getPluginManager()->registerEvents($this, $this);
        $this->getScheduler()->scheduleRepeatingTask(new HologramRefreshTask($this), 20 * 15);
    }

    protected function onDisable() : void{
        $this->saveRegions();
    }

    public function onCommand(CommandSender $sender, Command $command, string $label, array $args) : bool{
        if(!$sender instanceof Player){
            $sender->sendMessage(TF::RED . "Команда доступна только игроку.");
            return true;
        }

        if(($args[0] ?? "help") === "help"){
            $sender->sendMessage(TF::GOLD . "---- " . TF::YELLOW . "Помощь по приватам" . TF::GOLD . " ----");
            $sender->sendMessage(TF::AQUA . "/rg add <ник>" . TF::WHITE . " — добавить игрока в приват, где вы стоите.");
            $sender->sendMessage(TF::AQUA . "/rg remove <ник>" . TF::WHITE . " — удалить игрока из текущего привата.");
            $sender->sendMessage(TF::GRAY . "Поставьте блок привата без шифта: железо 5x5, алмаз 8x8, изумрудная руда 15x15, незерит 20x20, древние обломки 30x30.");
            return true;
        }

        $sub = strtolower($args[0] ?? "");
        if(!in_array($sub, ["add", "remove"], true) || !isset($args[1])){
            $sender->sendMessage(TF::RED . "Используйте: /rg add <ник>, /rg remove <ник> или /rg help");
            return true;
        }

        $regionId = $this->getRegionIdAt($sender->getPosition()->getWorld(), $sender->getPosition());
        if($regionId === null){
            $sender->sendMessage(TF::RED . "Вы не стоите в регионе.");
            return true;
        }

        $region = &$this->regions[$regionId];
        if(strtolower((string) $region["owner"]) !== strtolower($sender->getName())){
            $sender->sendMessage(TF::RED . "Управлять этим регионом может только владелец.");
            return true;
        }

        $nick = $args[1];
        $members = array_map('strtolower', (array) ($region["members"] ?? []));
        if($sub === "add"){
            if(!in_array(strtolower($nick), $members, true)){
                $region["members"][] = $nick;
                $this->saveRegions();
            }
            $sender->sendMessage(TF::GREEN . "Игрок " . TF::WHITE . $nick . TF::GREEN . " добавлен в приват.");
        }else{
            $region["members"] = array_values(array_filter((array) $region["members"], static fn(string $member) : bool => strtolower($member) !== strtolower($nick)));
            $this->saveRegions();
            $sender->sendMessage(TF::YELLOW . "Игрок " . TF::WHITE . $nick . TF::YELLOW . " удалён из привата.");
        }

        return true;
    }

    public function onBlockPlace(BlockPlaceEvent $event) : void{
        $player = $event->getPlayer();
        foreach($event->getTransaction()->getBlocks() as [$x, $y, $z, $block]){
            if(!$this->canBuild($player, new Vector3($x, $y, $z))){
                $event->cancel();
                return;
            }
        }

        if($player->isSneaking()){
            return;
        }

        foreach($event->getTransaction()->getBlocks() as [$x, $y, $z, $block]){
            $type = $this->getPrivateType($block);
            if($type === null){
                continue;
            }

            $world = $player->getWorld();
            $region = $this->makeRegion($type, $player, $world, $x, $y, $z);
            if($this->intersectsAny($region)){
                $event->cancel();
                $player->sendTip(TF::RED . "Регион пересекается с другим");
                return;
            }

            $this->regions[$region["id"]] = $region;
            $this->saveRegions();
            $player->sendMessage($region["color"] . $region["name"] . " установлен");
            $world->addSound(new Vector3($x + 0.5, $y + 0.5, $z + 0.5), new XpLevelUpSound(8), [$player]);
            $this->sendHolograms($player);
        }
    }

    public function onMove(PlayerMoveEvent $event) : void{
        if($event->getFrom()->floor()->equals($event->getTo()->floor())){
            return;
        }
        $player = $event->getPlayer();
        $regionId = $this->getRegionIdAt($player->getWorld(), $event->getTo());
        $old = $this->insideRegionByPlayer[strtolower($player->getName())] ?? null;
        if($regionId !== null && $regionId !== $old){
            $region = $this->regions[$regionId];
            if(!$this->canAccess($player->getName(), $region)){
                $player->sendTip(self::QUESTION_MARK . " Вы вторглись в приват " . (string) $region["owner"]);
            }
        }
        $this->insideRegionByPlayer[strtolower($player->getName())] = $regionId ?? "";
    }

    public function onJoin(PlayerJoinEvent $event) : void{
        $this->getScheduler()->scheduleDelayedTask(new HologramSendTask($this, $event->getPlayer()), 20);
    }

    public function onBreak(BlockBreakEvent $event) : void{
        $pos = $event->getBlock()->getPosition();
        if(!$this->canBuild($event->getPlayer(), $pos)){
            $event->cancel();
            return;
        }

        $regionId = $this->getRegionIdAt($pos->getWorld(), $pos);
        if($regionId !== null && (int) $this->regions[$regionId]["x"] === $pos->getFloorX() && (int) $this->regions[$regionId]["y"] === $pos->getFloorY() && (int) $this->regions[$regionId]["z"] === $pos->getFloorZ()){
            unset($this->regions[$regionId]);
            $this->saveRegions();
            $event->getPlayer()->sendMessage(TF::YELLOW . "Приват удалён.");
        }
    }

    public function onInteract(PlayerInteractEvent $event) : void{
        if(!$this->canBuild($event->getPlayer(), $event->getBlock()->getPosition())){
            $event->cancel();
        }
    }

    public function onExplode(EntityExplodeEvent $event) : void{
        $event->setBlockList(array_values(array_filter($event->getBlockList(), fn(Block $block) : bool => $this->getRegionIdAt($block->getPosition()->getWorld(), $block->getPosition()) === null)));
    }

    public function sendHolograms(?Player $target = null) : void{
        $players = $target === null ? $this->getServer()->getOnlinePlayers() : [$target];
        foreach($this->regions as $region){
            if((string) ($region["title"] ?? "") === ""){
                continue;
            }
            $world = $this->getServer()->getWorldManager()->getWorldByName((string) $region["world"]);
            if(!$world instanceof World){
                continue;
            }
            $particle = new FloatingTextParticle((string) $region["title"]);
            $pos = new Vector3((float) $region["x"] + 0.5, (float) $region["y"] + 1.35, (float) $region["z"] + 0.5);
            foreach($players as $player){
                if($player->getWorld()->getFolderName() === $world->getFolderName()){
                    $world->addParticle($pos, $particle, [$player]);
                }
            }
        }
    }

    private function canBuild(Player $player, Vector3 $pos) : bool{
        $regionId = $this->getRegionIdAt($player->getWorld(), $pos);
        return $regionId === null || $this->canAccess($player->getName(), $this->regions[$regionId]);
    }

    /** @return array<string, mixed> */
    private function makeRegion(array $type, Player $player, World $world, int $x, int $y, int $z) : array{
        $size = (int) $type["size"];
        $minX = $x - intdiv($size - 1, 2);
        $minZ = $z - intdiv($size - 1, 2);
        return [
            "id" => strtolower($world->getFolderName()) . ":" . $x . ":" . $y . ":" . $z,
            "world" => $world->getFolderName(),
            "owner" => $player->getName(),
            "members" => [],
            "name" => $type["name"],
            "title" => $type["title"],
            "color" => $type["color"],
            "x" => $x,
            "y" => $y,
            "z" => $z,
            "minX" => $minX,
            "maxX" => $minX + $size - 1,
            "minZ" => $minZ,
            "maxZ" => $minZ + $size - 1,
            "minY" => $world->getMinY(),
            "maxY" => $world->getMaxY(),
        ];
    }

    /** @return array{name:string,title:string,color:string,size:int}|null */
    private function getPrivateType(Block $block) : ?array{
        $id = $block->getTypeId();
        return match($id){
            VanillaBlocks::IRON()->getTypeId() => ["name" => "Железный приват", "title" => TF::GRAY . "Железный приват", "color" => TF::GRAY, "size" => 5],
            VanillaBlocks::DIAMOND()->getTypeId() => ["name" => "Алмазный приват", "title" => TF::AQUA . "Алмазный приват", "color" => TF::AQUA, "size" => 8],
            VanillaBlocks::EMERALD_ORE()->getTypeId() => ["name" => "Изумрудный приват", "title" => TF::GREEN . "Изумрудный приват", "color" => TF::GREEN, "size" => 15],
            VanillaBlocks::NETHERITE()->getTypeId() => ["name" => "Незеритовый приват", "title" => TF::LIGHT_PURPLE . "Незеритовый приват", "color" => TF::LIGHT_PURPLE, "size" => 20],
            VanillaBlocks::ANCIENT_DEBRIS()->getTypeId() => ["name" => "Незеритовая руда", "title" => "", "color" => TF::LIGHT_PURPLE, "size" => 30],
            default => null,
        };
    }

    /** @param array<string, mixed> $region */
    private function intersectsAny(array $region) : bool{
        foreach($this->regions as $other){
            if($region["world"] !== $other["world"]){
                continue;
            }
            if($region["maxX"] >= $other["minX"] && $region["minX"] <= $other["maxX"] && $region["maxZ"] >= $other["minZ"] && $region["minZ"] <= $other["maxZ"]){
                return true;
            }
        }
        return false;
    }

    private function getRegionIdAt(World $world, Vector3 $pos) : ?string{
        $x = (int) floor($pos->getX());
        $z = (int) floor($pos->getZ());
        foreach($this->regions as $id => $region){
            if($world->getFolderName() === $region["world"] && $x >= $region["minX"] && $x <= $region["maxX"] && $z >= $region["minZ"] && $z <= $region["maxZ"]){
                return (string) $id;
            }
        }
        return null;
    }

    /** @param array<string, mixed> $region */
    private function canAccess(string $name, array $region) : bool{
        $lower = strtolower($name);
        return $lower === strtolower((string) $region["owner"]) || in_array($lower, array_map('strtolower', (array) ($region["members"] ?? [])), true);
    }

    private function saveRegions() : void{
        $this->storage->setAll($this->regions);
        $this->storage->save();
    }
}
