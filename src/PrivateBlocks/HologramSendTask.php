<?php

declare(strict_types=1);

namespace PrivateBlocks;

use pocketmine\player\Player;
use pocketmine\scheduler\Task;

final class HologramSendTask extends Task{
    public function __construct(private BlockPrivates $plugin, private Player $player){
    }

    public function onRun() : void{
        if($this->player->isOnline()){
            $this->plugin->sendHolograms($this->player);
        }
    }
}
