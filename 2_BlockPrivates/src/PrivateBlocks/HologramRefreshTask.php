<?php

declare(strict_types=1);

namespace PrivateBlocks;

use pocketmine\scheduler\Task;

final class HologramRefreshTask extends Task{
    public function __construct(private BlockPrivates $plugin){
    }

    public function onRun() : void{
        $this->plugin->sendHolograms();
    }
}
