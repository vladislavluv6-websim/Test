#include <jni.h>
#include <android/native_activity.h>
#include <errno.h>

#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <iostream>
#include <fstream>
#include <stdio.h>
#include <sstream>
#include <vector>
#include <unordered_set>
#include <random>
#include <algorithm>
#include <ghc/filesystem.hpp>
namespace fs = ghc::filesystem;
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include <signal.h>
#include <pthread.h>
#include <codecvt>
#include <chrono>
#include <queue>

#include "ImGui/imgui_internal.h"
#include "ImGui/imgui.h"
#include "ImGui/backends/imgui_impl_android.h"
#include "ImGui/backends/imgui_impl_opengl3.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>

#include <sys/system_properties.h>

#include <string>
#include <cmath>
#include <android/keycodes.h>

//====================================
#include "ImGui/FONTS/DEFAULT.h"
//=====================================
#include <Includes/Utils.h>
#include <Includes/Dobby/dobby.h>
#if defined(__aarch64__)
#include "Arm64/And64InlineHook.hpp"
#else
#include "Substrate/SubstrateHook.h"
#endif
#include "KittyMemory/MemoryPatch.h"
#include "KittyScanner/KittyScanner.hpp"
#include "ENCRYPT/obfuscate.h"
#include "ENCRYPT/oxorany.h"
#include "RGB.h"
#include "Chams.h"
#include "Includes/nlohmann/json.hpp"
#include "Includes/json.h"

typedef uint32_t _DWORD;
typedef uint64_t _QWORD;
#define __int8 char
#define __int16 short
#define __int32 int
#define __int64 long long
#define _BYTE unsigned char


using json = nlohmann::json;
int screenWidth = 0;
int screenHeight = 0;
float scaleGlobal, calcResX;
bool g_Initialized = false;
ImGuiWindow* g_window = NULL;
string GameName = oxorany("com.mojang.minecraftpe");
const char* MineLib = OBFUSCATE("libminecraftpe.so");


struct My_Patches {MemoryPatch
Font0, Font1, Font2, Font3, Font4, Font5, Font6, Font7, Font8, Font9, Font10,
NoHurtCam, NoCamDistortion, NoBoatRotation, NoCamSleep, PlaceCamera,
NoSlowDown,
LavaDrown, WaterDrown,
SlowDownTriggers,
Noclip,
AutoClickMine,
XrayCameraThird,
NoEmoteUseCooldown,
NoBlur1, NoBlur2,
FullBright,
AntiKnockBack,
NativeKeyHandler,
NoCaveVignette
;} hexPatches;


#include "offsets.h"
#include "dark_theme.h"
#include "Vector.h"
#include "hook_classes.h"
#include "variables.h"
#include "hook_utils.h"
#include "menu_utils.h"


extern "C" {
    JNIEXPORT void JNICALL Java_com_mojang_minecraftpe_platforms_Platform2_init(JNIEnv* env, jclass cls);
    JNIEXPORT void JNICALL Java_com_mojang_minecraftpe_platforms_Platform2_resize(JNIEnv* env, jobject obj, jint width, jint height);
    JNIEXPORT void JNICALL Java_com_mojang_minecraftpe_platforms_Platform2_step(JNIEnv* env, jobject obj);
    JNIEXPORT void JNICALL Java_com_mojang_minecraftpe_platforms_Platform2_imgui_Shutdown(JNIEnv* env, jobject obj);
    JNIEXPORT void JNICALL Java_com_mojang_minecraftpe_platforms_Platform2_MotionEventClick(JNIEnv* env, jobject obj,jboolean down,jfloat PosX,jfloat PosY);
    JNIEXPORT jstring JNICALL Java_com_mojang_minecraftpe_platforms_Platform2_getWindowRect(JNIEnv *env, jobject thiz);
};

JNIEXPORT void JNICALL
Java_com_mojang_minecraftpe_platforms_Platform2_init(JNIEnv* env, jclass cls) {

    //SetUpImGuiContext
    if(!g_Initialized) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
	
    //Set ImGui Style
    ImGui::StyleColorsClassic();
	embraceTheDarkness();
 
    // Setup Platform/Renderer backends
    ImGui_ImplOpenGL3_Init(oxorany("#version 100"));

    ImFontConfig CustomFont;
    CustomFont.FontDataOwnedByAtlas = false;

	static const ImWchar ranges[] = {
        0x0020, 0x00FF, // Basic Latin + Latin Supplement
        0x0100, 0x024F, // Extended Latin
        0x0370, 0x03FF, // Greek
        0x0400, 0x04FF, // Cyrillic
        0x0500, 0x052F, // Cyrillic Supplement
        0x2000, 0x206F, // General Punctuation
        0x2070, 0x209F, // Superscripts and Subscripts
        0x20A0, 0x20CF, // Currency Symbols
        0x2100, 0x214F, // Letterlike Symbols
        0x2150, 0x218F, // Number Forms
        0x2190, 0x21FF, // Arrows
        0x2200, 0x22FF, // Mathematical Operators
        0x2300, 0x23FF, // Miscellaneous Technical
        0x2400, 0x243F, // Control Pictures
        0x2440, 0x245F, // Optical Character Recognition
        0x2460, 0x24FF, // Enclosed Alphanumerics
        0x2500, 0x257F, // Box Drawing
        0x2580, 0x259F, // Block Elements
        0x25A0, 0x25FF, // Geometric Shapes
        0x2600, 0x26FF, // Miscellaneous Symbols
        0x2700, 0x27BF, // Dingbats
        0x3000, 0x303F, // CJK Symbols and Punctuation
        0x3040, 0x309F, // Hiragana
        0x30A0, 0x30FF, // Katakana
        0x4E00, 0x9FFF, // CJK Unified Ideographs
        0xFF00, 0xFFEF, // Halfwidth and Fullwidth Forms
        0,
    };

    io.Fonts->AddFontFromMemoryTTF(const_cast<std::uint8_t*>(Custom3), sizeof(Custom3), 30.f, &CustomFont, ranges);

	static bool loadedCfg = false;
     if (!loadedCfg){
         std::ifstream file(oxorany("/data/data/") + GameName + oxorany("/xal/config.json"));
         if (file.good()){
             loadConfig(oxorany("/data/data/") + GameName + oxorany("/xal/config.json"));
         }
         loadedCfg = true;
     }
	g_Initialized = true;
	}
    return;
}

JNIEXPORT void JNICALL
Java_com_mojang_minecraftpe_platforms_Platform2_resize(JNIEnv* env, jobject obj, jint width, jint height) {
	if (g_Initialized) {
    screenWidth = (int) width;
    screenHeight = (int) height;
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.DisplaySize = ImVec2((float)screenWidth, (float)screenHeight);
	static bool isLoaded = false;
	if (!isLoaded) {
		ImGuiStyle& style = ImGui::GetStyle();
	    scaleGlobal = calculateResolutionScale(screenWidth, screenHeight, 2480, 1116);
		calcResX = calculateResolutionX(screenWidth, 2480);
	    style.ScaleAllSizes(scaleGlobal * 3.5);
        io.FontGlobalScale = scaleGlobal;
		style.WindowBorderSize                  = scaleGlobal * 2;
        style.ChildBorderSize                   = scaleGlobal * 2;
        style.PopupBorderSize                   = scaleGlobal * 2;
        style.FrameBorderSize                   = scaleGlobal * 2;
        style.TabBorderSize                     = scaleGlobal * 2;
		isLoaded = true;
	}
	}
	return;
}

#include "custom_modules.h"

void Patches() {
    // Version-specific memory patches removed: the old ones used MC 1.21 offsets
    // and do not apply to 1.26.60.4. Add per-frame logic in OnFrame() (custom_modules.h).
    OnFrame();
}

#include "menu.h"

JNIEXPORT void JNICALL
Java_com_mojang_minecraftpe_platforms_Platform2_step(JNIEnv* env, jobject obj) {
    if (g_Initialized) {
        removeDirectory(oxorany("/data/data/") + GameName + oxorany("/app_lib"));
        saveConfig(oxorany("/data/data/") + GameName + oxorany("/xal/config.json"));
        ImGuiIO& io = ImGui::GetIO();
        
        //Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
        
        BeginDraw();
        
        RenderNotifications();
        
        ImGui::EndFrame();
        
        ImGui::Render();
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
}

JNIEXPORT void JNICALL Java_com_mojang_minecraftpe_platforms_Platform2_imgui_Shutdown(JNIEnv* env, jobject obj){
    if (!g_Initialized)
        return;
     // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplAndroid_Shutdown();
    ImGui::DestroyContext();
    g_Initialized=false;
}

JNIEXPORT void JNICALL Java_com_mojang_minecraftpe_platforms_Platform2_MotionEventClick(JNIEnv* env, jobject obj,jboolean down,jfloat PosX,jfloat PosY){
	if (g_Initialized) {
	ImGuiIO & io = ImGui::GetIO();
	io.MousePos = ImVec2(PosX, PosY);
	io.MouseDown[0] = down;
	}
}

JNIEXPORT jstring JNICALL Java_com_mojang_minecraftpe_platforms_Platform2_getWindowRect(JNIEnv *env, jobject thiz) {
    //get drawing window
    // TODO: accomplish getWindowSizePos()
	if (g_Initialized) {
    char result[512]="0|0|0|0";
	if (!panic) {
		if(g_window){
            sprintf(result,"%d|%d|%d|%d",(int)g_window->Pos.x,(int)g_window->Pos.y,(int)g_window->Size.x,(int)g_window->Size.y);
        }
	}
    return env->NewStringUTF(result);
	}
}

void *(*old_Intersects)(Mob *thiz, vec3_t *a2, vec3_t *a3);
void *Intersects(Mob *thiz, vec3_t *a2, vec3_t *a3) {
	if (panic) return old_Intersects(thiz, a2, a3);
	if (inMenu) return old_Intersects(thiz, a2, a3);
	if (thiz == localPlayer) {
		
		if (look360) {
			if (notSaved) {
				saveAngle = *thiz->getRotation();
				saveYHeadRot = thiz->getYHeadRot();
				notSaved = false;
		    }
			thiz->setRotationX(saveAngle.x);
			thiz->setRotationY(saveAngle.y);
			thiz->setYHeadRot(saveYHeadRot);
		} else {
			notSaved = true;
		}
		
		if (!isLANGame) {
		    if (derp) {
				thiz->setYHeadRot(derpGradys);
			    thiz->setYBodyRotation(derpGradys);
		    }
		}
		
		if (fireballRotation || itemRotation || !mobList.empty() || !playerList.empty()) {
			if (rotations) {
                if (!isLANGame && !emulateNativeAttack) thiz->setYBodyRotation(auraAngle.y);
			    thiz->setYHeadRot(auraAngle.y);
                thiz->setRotationX(auraAngle.x);
            }
            if (targetLock) {
                thiz->setRotationY(auraAngle.y);
            }
		}
    }
    return old_Intersects(thiz, a2, a3);
}

void *(*old_NormalTick)(LocalPlayer *thiz);
void *NormalTick(LocalPlayer *thiz) {
	if (panic) return old_NormalTick(thiz);
	localPlayer = NULL;
	if (inMenu) {
		isLoaded = true;
		return old_NormalTick(thiz);
	}
	
	//inMemoryFunction = format("Instance: %llX", thiz);
	
	if (isLoaded) {
		thiz->displayClientMessage(oxorany("§l§0[§5Apollon Client§0]§r Good Luck, Have Fun!"), "");
		isLoaded = false;
	}
	
	localPlayer = thiz;
	vec2_t *playerRot = thiz->getRotation();
	vec3_t *playerPos = thiz->getPosition();
	globalPlayerRot = *playerRot;
	globalPlayerPos = *playerPos;
	Level *myLevel = thiz->getLevel();
	GameMode *myGameMode = thiz->getGameMode();
	BlockSource *myBlockSource = thiz->getDimensionBlockSource();

	if (myBlockSource && xrayDefault) {
    for (int i = renderOresESP.size() - 1; i >= 0; i--) {
        vec3_t orePosition = {(float)renderOresESP[i].orePos.x, (float)renderOresESP[i].orePos.y, (float)renderOresESP[i].orePos.z};
        vec3_t camPos = {CameraPos.x, CameraPos.y, CameraPos.z};
        float distanceToBlock = distanceTo(&camPos, &orePosition);
        
        if (distanceToBlock > 32.0f) {
            renderOresESP.erase(renderOresESP.begin() + i);
            removedCount++;
            continue;
        }
        
        // ПРОВЕРКА НА AIR БЛОК
        Block *currentBlock = myBlockSource->getBlock(renderOresESP[i].orePos);
        if (!allowToXrayRender(renderOresESP[i].oreName) || currentBlock->isAir()) {
            renderOresESP.erase(renderOresESP.begin() + i);
            removedCount++;
			continue;
        }
    }
    
    if (removedCount >= 32) {
        rebuildChunkGeometry = true;
        removedCount = 0;
    }
} else {
    renderOresESP.clear();
}
	
	if (showPosition) {
	    playerPosX = playerPos->x;
	    playerPosY = playerPos->y;
	    playerPosY = playerPosY - 1.62;
	    playerPosZ = playerPos->z;
	}
	
	if (thiz->getHealth() == 0) {
		if (deathPosition) {
			vec3_t *deathPos = thiz->getPosition();
	        playerDeathPosX = deathPos->x;
	        playerDeathPosY = deathPos->y;
	        playerDeathPosY = playerDeathPosY - 1.62;
	        playerDeathPosZ = deathPos->z;
		}
		return old_NormalTick(thiz);
	}
	
	if (damageFly) {
		myGameMode->attack(thiz, true);
		myGameMode->attack(thiz, true);
		thiz->swing();
	}
	
	if (!isLANGame) {
		#if defined(__ARM_ARCH_7A__)
		LocalPlayer *EntityAccess = (LocalPlayer *)((char *)thiz + 4);
		#else
		LocalPlayer *EntityAccess = (LocalPlayer *)((char *)thiz + 8);
		#endif
	    if (strafe) {
			EntityAccess->setOnGround(1, true);
			EntityAccess->setIsJumping(0, true);
		}
		if (noJumpDelay) EntityAccess->setNoJumpDelay(1, 0);
		if (alwaysSprinting) thiz->setSprinting(true);
	    if (ladderFly) thiz->setClimbing(true);
	    if (step) {
	        thiz->initializeMaxAutoStep(1.0625);
	    } else {
		    thiz->initializeMaxAutoStep(0.5625);
	    }
		if (creativeFly) {
			thiz->getAbilities()->getLayer(2)->setAbility(10, true);
			activatedCreativeFly = true;
		} else if (activatedCreativeFly && !creativeFly) {
			if (thiz->isAttackableGamemode()) thiz->getAbilities()->getLayer(2)->setAbility(10, false);
			activatedCreativeFly = false;
		}
		if (forceCreativeFly) {
			thiz->getAbilities()->getLayer(2)->setAbility(9, true);
			activatedForceCreativeFly = true;
		} else if (activatedForceCreativeFly && !forceCreativeFly) {
			if (thiz->isAttackableGamemode()) thiz->getAbilities()->getLayer(2)->setAbility(9, false);
			activatedCreativeFly = false;
		}
	} else thiz->initializeMaxAutoStep(0.5625);
	
	std::vector<Mob*> actors = myLevel->getRuntimeActorList();
	nearestMob = NULL;
	fireballRotation = false;
	itemRotation = false;
	mobList.clear();
	
	for (Mob *object : actors) {
		if (object != thiz) {
			if (antiInvis) object->setInvisible(false);
			vec3_t *targetPos = object->getPosition();
			vec2_t *targetHitBox = object->getAABBDim();
			vec2_t calcTargetHitBox = *targetHitBox;
			float distanceToActor = mobDistanceTo(playerPos, targetPos, calcTargetHitBox.y);
			
			int ActorId = object->getEntityTypeId();
			if (distanceToActor <= 10 && ActorId == 4194389) {
			    if (antiFireball) {
					if (rotations) {
                        vec3_t calcObjPos = *targetPos;
                        auraAngle = globalPlayerPos.CalcPlayerAngle(calcObjPos);
						fireballRotation = true;
				    }
					myGameMode->attack(object, true);
			        if (swing) thiz->swing();
				}
				continue;
			} else if (distanceToActor <= 7 && ActorId == 64) {
			    if (killItems) {
					if (rotations) {
                        vec3_t calcObjPos = *targetPos;
                        auraAngle = globalPlayerPos.CalcPlayerAngle(calcObjPos);
						itemRotation = true;
				    }
				    myGameMode->attack(object, true);
			        if (swing) thiz->swing();
			    }
				continue;
			} else {
				if (mobAura) {
				    if (distanceToActor <= mobAuraRange && object->getHealth() != 0 && MobDetected(ActorId)) mobList.push_back(object);
                }
			}
		}
	}
	
	nearestTarget = NULL;
	nearestKillTarget = NULL;
	nearFriendList.clear();
	playerList.clear();
	nearPlayerList.clear();
	nearPlayer.clear();
	std::vector<Player*> nearPlayers;
	
    myLevel->getGameplayUserManager()->forEachActivePlayer([&](Player& objectt) {
        nearPlayers.push_back(&objectt);
		return true;
    }, true);

	for (Player *objectt : nearPlayers) {
		 if (objectt == thiz) continue;
		vec2_t* AABB = objectt->getAABBDim();
		const std::string PlayerName = objectt->getName();
		const std::string PlayerNameTag = objectt->getNameTag();
		vec2_t Hitbox = *AABB;
				
        if (antiBot) {
            if (PlayerName.length() <= 1) continue;
			if (PlayerNameTag.length() <= 1) continue;
            if (!AABB) continue;
            if (Hitbox.y < 0.05f || Hitbox.x < 0.05f || Hitbox.y > 2.1f || Hitbox.x > 10.9f) continue;
        }
		
			bool isFriend = false;
			bool isBotList = false;

			for (std::string currentPlayer : botList) {
				if (currentPlayer == PlayerName) {
				    isBotList = true;
					break;
				}
			}
			
			if (!isBotList) {
				for (std::string currentPlayer : friendList) {
				    if (currentPlayer == PlayerName) {
					    if (playerNear) nearFriendList.push_back(PlayerName);
				        isFriend = true;
						break;
				    }
			    }
			}
			
			bool isAttackable = objectt->isAttackableGamemode();
			if (!isFriend && !isBotList) {
				AABB->x = hitBox ? hitBoxValue : 0.6;
				nearPlayer.push_back(PlayerName);
			}
					
			int targetHealth = objectt->getHealth();
			vec3_t* targetPos = objectt->getPosition();
			
            nearPlayerList.push_back({PlayerName, PlayerNameTag, targetPos, Hitbox, targetHealth, objectt->getHurtTime(), objectt->getPlatform(), !isAttackable, isFriend, isBotList});
					
            if (killAura) {
				float distanceTo = hackDistanceTo(playerPos, targetPos);
				if (distanceTo <= rotationsRange) {
                    if (isAttackable && !isFriend && !isBotList && targetHealth != 0) {
                        if (rotations || targetLock) {
                            if (distanceTo <= rotationsRange) {
                                playerList.push_back(objectt);
                            }
                        } else {
                            if (distanceTo <= killRange) {
                                playerList.push_back(objectt);
                            }
                        }
                    }
			    }
            }
	}
	
		if (!mobList.empty()) {
			switch (auraType) {
				case 0: {
					if (rotations || targetLock) {
						vec3_t *objPos = mobList[0]->getPosition();
						vec2_t *hitBox = mobList[0]->getAABBDim();
                        vec3_t calcObjPos = *objPos;
						vec2_t calcHitBox = *hitBox;
                        auraAngle = globalPlayerPos.CalcMobAngle(calcObjPos, calcHitBox.y);
					}
					bool canHurt = true;
					if (hurtCheck) canHurt = mobList[0]->canBeHurted();
					if (canMobAuraDamage && canHurt) {
						myGameMode->attack(mobList[0], true);
                        if (swing) thiz->swing();
                        canMobAuraDamage = false;
                    }
				break;
				}
				case 1: {
					float nearestMobDistance = 9999;
                    for (Mob *potentialTarget : mobList) {
						if (potentialTarget != NULL) {
							vec2_t *potentialHitBox = potentialTarget->getAABBDim();
							vec2_t calcPotentialHitBox = *potentialHitBox;
							float potentialDistance = mobDistanceTo(playerPos, potentialTarget->getPosition(), calcPotentialHitBox.y);
                            if (potentialDistance < nearestMobDistance && potentialDistance <= mobAuraRange) {
                                nearestMobDistance = potentialDistance;
                                nearestMob = potentialTarget;
                            }
						}
                    }
					if (nearestMob != NULL) {
						//inMemoryFunction = nearestMob->getNameTag();
						//nearActorId = nearestMob->getEntityTypeId();
						if (rotations || targetLock) {
							vec3_t* objPos = nearestMob->getPosition();
							vec2_t *hitBox = nearestMob->getAABBDim();
                            vec3_t calcObjPos = *objPos;
							vec2_t calcHitBox = *hitBox;
                            auraAngle = globalPlayerPos.CalcMobAngle(calcObjPos, calcHitBox.y);
						}
						bool canHurt = true;
						if (hurtCheck) canHurt = nearestMob->canBeHurted();
                        if (canMobAuraDamage && canHurt) {
                            myGameMode->attack(nearestMob, true);
                            if (swing) thiz->swing();
                            canMobAuraDamage = false;
                        }
					}
                break;
                }
				case 2: {
					float nearestMobDistance = 9999;
                    for (Mob *potentialTarget : mobList) {
						if (potentialTarget != NULL) {
							vec2_t *potentialHitBox = potentialTarget->getAABBDim();
							vec2_t calcPotentialHitBox = *potentialHitBox;
							float potentialDistance = mobDistanceTo(playerPos, potentialTarget->getPosition(), calcPotentialHitBox.y);
                            if (potentialDistance < nearestMobDistance && potentialDistance <= mobAuraRange) {
                                nearestMobDistance = potentialDistance;
                                nearestMob = potentialTarget;
                            }
						}
                    }
					if (nearestMob != NULL) {
						if (rotations || targetLock) {
							vec3_t* objPos = nearestMob->getPosition();
							vec2_t *hitBox = nearestMob->getAABBDim();
                            vec3_t calcObjPos = *objPos;
							vec2_t calcHitBox = *hitBox;
                            auraAngle = globalPlayerPos.CalcMobAngle(calcObjPos, calcHitBox.y);
						}
					    for (Mob*actors : mobList) {
						    if (maxMobAuraTick > currentMobTick) {
						        currentMobTick = currentMobTick + 1;
					        } else {
								bool canHurt = true;
						        if (hurtCheck) canHurt = actors->canBeHurted();
								if (canHurt) {
									myGameMode->attack(actors, true);
					                if (swing) thiz->swing();
						            currentMobTick = 1;
								}
						    }
					    }
					}
				break;
				}
			}
		}

    if (!playerList.empty()) {
        switch (auraType) {
			case 0: {
				vec3_t* objPos = playerList[0]->getPosition();
				if (rotations || targetLock) {
                    vec3_t calcObjPos = *objPos;
                    auraAngle = globalPlayerPos.CalcPlayerAngle(calcObjPos);
				}
				if (hackDistanceTo(playerPos, objPos) <= killRange) {
					bool canHurt = true;
					if (hurtCheck) canHurt = playerList[0]->canBeHurted();
					if (canKillAuraDamage && canHurt) {
						if (!emulateNativeAttack) {
                            myGameMode->attack(playerList[0], true);
						} else {
							doHandleAttack = true;
						}
                        if (swing) thiz->swing();
                        canKillAuraDamage = false;
                    }
				}
			break;
			}
            case 1: {
                float nearestDistance = 9999;
                for (Player *potentialTarget : playerList) {
					if (potentialTarget != NULL) {
						float potentialDistance = hackDistanceTo(playerPos, potentialTarget->getPosition());
                        if (potentialDistance < nearestDistance && potentialDistance <= rotationsRange) {
                            nearestDistance = potentialDistance;
                            nearestTarget = potentialTarget;
							if (potentialDistance <= killRange) {
								nearestKillTarget = potentialTarget;
							}
                        }
					}
                }
				if (nearestTarget != NULL) {
					//inMemoryFunction = std::to_string(*(int *)((uint64_t)nearestTarget + 0x47C));
					if (rotations || targetLock) {
                        vec3_t* objPos = nearestTarget->getPosition();
                        vec3_t calcObjPos = *objPos;
                        auraAngle = globalPlayerPos.CalcPlayerAngle(calcObjPos);
					}
					if (nearestKillTarget != NULL) {
						bool canHurt = true;
					    if (hurtCheck) canHurt = nearestTarget->canBeHurted();
						if (canKillAuraDamage && canHurt) {
						    if (!emulateNativeAttack) {
                                myGameMode->attack(nearestTarget, true);
						    } else {
							    doHandleAttack = true;
						    }
                            if (swing) thiz->swing();
                            canKillAuraDamage = false;
                        }
					}
				}
            break;
			}
            case 2: {
				float nearestDistance = 9999;
                for (Player *potentialTarget : playerList) {
					if (potentialTarget != NULL) {
						float potentialDistance = hackDistanceTo(playerPos, potentialTarget->getPosition());
                        if (potentialDistance < nearestDistance && potentialDistance <= rotationsRange) {
                            nearestDistance = potentialDistance;
                            nearestTarget = potentialTarget;
							if (potentialDistance <= killRange) {
								nearestKillTarget = potentialTarget;
							}
                        }
					}
                }
				if (nearestTarget != NULL) {
					if (rotations || targetLock) {
                        vec3_t* objPos = nearestTarget->getPosition();
                        vec3_t calcObjPos = *objPos;
                        auraAngle = globalPlayerPos.CalcPlayerAngle(calcObjPos);
					}
				    if (nearestKillTarget != NULL) {
						for (Player *actors : playerList) {
							if (maxKillTick > currentKillTick) {
                                currentKillTick = currentKillTick + 1;
                            } else {
								bool canHurt = true;
						        if (hurtCheck) canHurt = actors->canBeHurted();
								if (canHurt) {
							        myGameMode->attack(actors, true);
                                    if (swing) thiz->swing();
							        currentKillTick = 1;
								}
						    }
						}
                    }
				}
            break;
			}
        }
    }
    return old_NormalTick(thiz);
}

void *(*old_PlayerAuthInputPacket)(void *a1, void *a2);
void *PlayerAuthInputPacket(void *a1, void *a2) {
	if (panic) return old_PlayerAuthInputPacket(a1, a2);
	if (!inMenu) {
		if (!isLANGame) {
		if (derp) {
		    if (derpGradys < 360) {
			    derpGradys = derpGradys + 30;
		    } else {
			    derpGradys = 0;
		    }
			#if defined(__ARM_ARCH_7A__)
			*(float *)((int)a1 + 36) = derpGradys;
			#else
			*(float *)((__int64)a1 + 48) = derpGradys;
			#endif
		}
		if (rotations) {
			if (fireballRotation || itemRotation || !mobList.empty() || !playerList.empty()) {
				#if defined(__ARM_ARCH_7A__)
				*(float *)((int)a1 + 36) = auraAngle.y;
				#else
				*(float *)((__int64)a1 + 48) = auraAngle.y;
				#endif
			}
		}
		}
	}
	return old_PlayerAuthInputPacket(a1, a2);
}

void *(*old_ClientInstance_onTick)(ClientInstance *thiz, void *a2, void *a3);
void *ClientInstance_onTick(ClientInstance *thiz, void *a2, void *a3) {
	CI = thiz;
	std::string currentScreenName = thiz->getScreenName();
	//inMemoryFunction = currentScreenName;
	if (currentScreenName == oxorany("hud_screen")) {
		inHudScreen = true;
		inMenu = false;
	} else {
		inHudScreen = false;
	}
	if (currentScreenName == oxorany("chat_screen")) {
		inChatScreen = true;
	} else {
		inChatScreen = false;
	}
	if (currentScreenName == oxorany("/hbui/index.html")) {
		manageWorldsScreen = true;
	} else {
		manageWorldsScreen = false;
	}
	if (panic) return old_ClientInstance_onTick(thiz, a2, a3);
	
	if (onFunctionSound) {
		thiz->playUI(oxorany("random.pop"), 0.2, 2);
		onFunctionSound = false;
	}
	if (offFunctionSound) {
		thiz->playUI(oxorany("random.pop2"), 0.2, 2);
		offFunctionSound = false;
	}
	if (errorFunctionSound) {
		thiz->playUI(oxorany("random.break"), 0.2, 2);
		errorFunctionSound = false;
	}
	if (inMenu) return old_ClientInstance_onTick(thiz, a2, a3);
	
	LevelRenderer *thisLevel = thiz->getLevelRenderer();
	//inMemoryFunction = format((void *)thiz);
	//inMemoryFunction = format((void *)thisLevel->getLevelRendererPlayer());
	
	if (rebuildChunkGeometry) {
		thisLevel->rebuildAllRenderChunkGeometry();
		rebuildChunkGeometry = false;
	}

	if (doHandleAttack) {
		thiz->handleDestoryOrAttackButtonPress();
		doHandleAttack = false;
	}
	
	if (autoClicker) {
		if (canClick) {
			thiz->handleDestoryOrAttackButtonPress();
			canClick = false;
		}
	}
    return old_ClientInstance_onTick(thiz, a2, a3);
}

void *(*old_HudPlayerRenderer_update)(void *a1, void *a2, void *a3, void *a4, void *a5, void *a6, void *a7, void *a8, void *a9, void *a10);
void *HudPlayerRenderer_update(void *a1, void *a2, void *a3, void *a4, void *a5, void *a6, void *a7, void *a8, void *a9, void *a10) {
	if (!panic) {
		if (showPaperdoll) {
			#if defined(__ARM_ARCH_7A__)
			*(float *)((int)a1 + 8) = 0.3;
			#else
			*(float *)((__int64)a1 + 12) = 0.3;
			#endif
		}
	}
    return old_HudPlayerRenderer_update(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
}

void *(*old_InLanGame1)(void *a1, void *a2, void *a3, void *a4, void *a5, void *a6, void *a7, void *a8, void *a9, void *a10);
void *InLanGame1(void *a1, void *a2, void *a3, void *a4, void *a5, void *a6, void *a7, void *a8, void *a9, void *a10) {
	if (!inMenu) isLANGame = true;
    return old_InLanGame1(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
}

void *(*old_InLanGame2)(void *a1, void *a2, void *a3, void *a4, void *a5, void *a6, void *a7, void *a8, void *a9, void *a10);
void *InLanGame2(void *a1, void *a2, void *a3, void *a4, void *a5, void *a6, void *a7, void *a8, void *a9, void *a10) {
	if (!inMenu) isLANGame = true;
    return old_InLanGame2(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
}

void **(*old_InMainMenu)(void **a1, void **a2, void **a3, void **a4, void **a5, void **a6, void **a7, void **a8, void **a9, void **a10);
void **InMainMenu(void **a1, void **a2, void **a3, void **a4, void **a5, void **a6, void **a7, void **a8, void **a9, void **a10) {
	inMenu = true;
	isLANGame = false;
	nearestTarget = NULL;
	nearestKillTarget = NULL;
	nearestMob = NULL;
	localPlayer = NULL;
	fireballRotation = false;
	itemRotation = false;
	playerList.clear();
	nearPlayerList.clear();
	nearPlayer.clear();
	nearFriendList.clear();
	mobList.clear();
	renderOresESP.clear();
    return old_InMainMenu(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
}

int (*old_LevelRendererCamera_FogState)(void *a1);
int LevelRendererCamera_FogState(void *a1) {
	if (!panic) {
		if (fogState != 0) return fogState - 1;
	}
    return old_LevelRendererCamera_FogState(a1);
}

float (*old_PlayerFov)(void *a1, void *a2, void *a3, void *a4);
float PlayerFov(void *a1, void *a2, void *a3, void *a4) {
	if (!panic) {
		if (enableFov) return globalPlayerFov =  old_PlayerFov(a1, a2, a3, a4) + fov;
	}
	return globalPlayerFov = old_PlayerFov(a1, a2, a3, a4);
}

void (*old_DropItemTime)(void *a1);
void DropItemTime(void *a1) {
	if (panic) return old_DropItemTime(a1);
	if (fastDrop) {
	    #if defined(__ARM_ARCH_7A__)
		float currentTime = *(float *)((int)a1 + 436);
	    if (currentTime < 35) *(float *)((int)a1 + 436) = 35;
		#else
		float currentTime = *(float *)((__int64)a1 + 692);
	    if (currentTime < 35) *(float *)((__int64)a1 + 692) = 35;
		#endif
	}
    return old_DropItemTime(a1);
}

void (*old_Gravity)(...);
#if defined(__ARM_ARCH_7A__)
void Gravity(void *a1, void *a2, void *a3, int a4, void *a5, void *a6) {
#else
void Gravity(void *a1, void *a2, void *a3, __int64 a4, void *a5, void *a6) {
#endif
    if (panic) return old_Gravity(a1, a2, a3, a4, a5, a6);
    if (!isLANGame) {
		float mine = *(float *)(a4 + 28);
	    *(float *)(a4 + 28) = mine + gravityValue + 0.08;
		if (fastFall) {
			if (mine < 0) *(float *)(a4 + 28) = -99;
		}
	}
    return old_Gravity(a1, a2, a3, a4, a5, a6);
}

int (*old_AppPlatform_getMaxPlayers)(void *thiz);
int AppPlatform_getMaxPlayers(void *thiz) {
	if (!panic) return myMaxPlayers;
    return old_AppPlatform_getMaxPlayers(thiz);
}

bool (*old_Entitlement_UnlockMarket)(void *a1, void *a2);
bool Entitlement_UnlockMarket(void *a1, void *a2) {
	if (!panic) {
		if (unlockMarket && !manageWorldsScreen) return true;
	}
    return old_Entitlement_UnlockMarket(a1, a2);
}

int (*old_GameMode_FastBridge)(int a1, int a2, int *a3, int a4, int a5, int a6);
int GameMode_FastBridge(int a1, int a2, int *a3, int a4, int a5, int a6) {
	if (panic) return old_GameMode_FastBridge(a1, a2, a3, a4, a5, a6);
	if (fastBridge) {
		old_GameMode_FastBridge(a1, a2, a3, a4, a5, a6);
		return 0;
	}
    return old_GameMode_FastBridge(a1, a2, a3, a4, a5, a6);
}

float (*old_GameMode_BlockReach)(void *a1, void *a2, void *a3);
float GameMode_BlockReach(void *a1, void *a2, void *a3) {
	if (!panic) {
		if (blockReach) return 12;
	}
    return old_GameMode_BlockReach(a1, a2, a3);
}

void *(*old_LevelRendererPlayer_setupFog)(void *a1, void *a2, void *a3);
void *LevelRendererPlayer_setupFog(void *a1, void *a2, void *a3) {
	if (panic) return old_LevelRendererPlayer_setupFog(a1, a2, a3);
	if (antiBlind) {
		#if defined(__ARM_ARCH_7A__)
		*(bool *)((int)a1 + 1682) = true;
		#else
		*(bool *)((__int64)a1 + 2450) = true;
		#endif
	}
    return old_LevelRendererPlayer_setupFog(a1, a2, a3);
}

void (*old_SpeedHack)(void *a1, void *a2, void *a3, void *a4, void *a5, void *a6, void *a7, void *a8, void *a9, void *a10, void *a11, void *a12, void *a13);
void SpeedHack(void *a1, void *a2, void *a3, void *a4, void *a5, void *a6, void *a7, void *a8, void *a9, void *a10, void *a11, void *a12, void *a13) {
	if (panic) return old_SpeedHack(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13);
	if (!isLANGame) {
	    for (int i = 0; i < speedValue - 1; ++i) {
		    old_SpeedHack(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13);
        }
	}
    return old_SpeedHack(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13);
}

float (*old_JumpHack)(void *a1, void *a2, void *a3, void *a4, void *a5, void *a6);
float JumpHack(void *a1, void *a2, void *a3, void *a4, void *a5, void *a6) {
	if (panic) return 0.42;
	if (!isLANGame) {
	    if (jumpValue != 1) {
		    return 0.42 + ((jumpValue - 1) / 10);
	    }
	}
    return 0.42;
}

void (*old_WaterSpeed)(void *a1, void *a2, void *a3, void *a4, void *a5, void *a6);
void WaterSpeed(void *a1, void *a2, void *a3, void *a4, void *a5, void *a6) {
	if (panic) return old_WaterSpeed(a1, a2, a3, a4, a5, a6);
	if (!isLANGame) {
	    for (int i = 0; i < waterSpeedValue - 1; ++i) {
		    old_WaterSpeed(a1, a2, a3, a4, a5, a6);
        }
	}
    return old_WaterSpeed(a1, a2, a3, a4, a5, a6);
}

void (*old_LavaSpeed)(void *a1, void *a2, void *a3, void *a4, void *a5, void *a6);
void LavaSpeed(void *a1, void *a2, void *a3, void *a4, void *a5, void *a6) {
	if (panic) return old_LavaSpeed(a1, a2, a3, a4, a5, a6);
	if (!isLANGame) {
	    for (int i = 0; i < lavaSpeedValue - 1; ++i) {
		    old_LavaSpeed(a1, a2, a3, a4, a5, a6);
        }
	}
    return old_LavaSpeed(a1, a2, a3, a4, a5, a6);
}


#if defined(__ARM_ARCH_7A__)
int *(*old_WasInWater)(_DWORD *a1, int a2);
int *WasInWater(_DWORD *a1, int a2) {
	if (panic) return old_WasInWater(a1, a2);
	if (!isLANGame) {
	    if (!inMenu && offWaterTrigger) {
		    *(_DWORD *)(a1[7] + 4 * ((((unsigned int)(a1[8] - a1[7]) >> 2) - 1) & a2)) = -1;
	    }
	}
	return old_WasInWater(a1, a2);
}

int *(*old_ActorHeadInWater)(_DWORD *a1, int a2);
int *ActorHeadInWater(_DWORD *a1, int a2) {
	if (panic) return old_ActorHeadInWater(a1, a2);
	if (!isLANGame) {
	    if (!inMenu && offWaterTrigger) {
		    *(_DWORD *)(a1[7] + 4 * ((((unsigned int)(a1[8] - a1[7]) >> 2) - 1) & a2)) = -1;
	    }
	}
	return old_ActorHeadInWater(a1, a2);
}

int *(*old_WaterSplashEffect)(_DWORD *a1, int a2);
int *WaterSplashEffect(_DWORD *a1, int a2) {
	if (panic) return old_WaterSplashEffect(a1, a2);
	if (!isLANGame) {
	    if (!inMenu && offWaterTrigger) {
		    *(_DWORD *)(a1[7] + 4 * ((((unsigned int)(a1[8] - a1[7]) >> 2) - 1) & a2)) = -1;
	    }
	}
	return old_WaterSplashEffect(a1, a2);
}


#else


__int64 (*old_WasInWater)(_QWORD *a1, int a2);
__int64 WasInWater(_QWORD *a1, int a2) {
	if (panic) return old_WasInWater(a1, a2);
	if (!isLANGame) {
	    if (!inMenu && offWaterTrigger) {
		    *(_QWORD *)(a1[7] + 8LL * (((unsigned int)((unsigned __int64)(a1[8] - a1[7]) >> 3) - 1) & a2)) = -1;
	    }
	}
	return old_WasInWater(a1, a2);
}

__int64 (*old_ActorHeadInWater)(_QWORD *a1, int a2);
__int64 ActorHeadInWater(_QWORD *a1, int a2) {
	if (panic) return old_ActorHeadInWater(a1, a2);
	if (!isLANGame) {
	    if (!inMenu && offWaterTrigger) {
		    *(_QWORD *)(a1[7] + 8LL * (((unsigned int)((unsigned __int64)(a1[8] - a1[7]) >> 3) - 1) & a2)) = -1;
	    }
	}
	return old_ActorHeadInWater(a1, a2);
}

__int64 (*old_WaterSplashEffect)(_QWORD *a1, int a2);
__int64 WaterSplashEffect(_QWORD *a1, int a2) {
	if (panic) return old_WaterSplashEffect(a1, a2);
	if (!isLANGame) {
	    if (!inMenu && offWaterTrigger) {
		    *(_QWORD *)(a1[7] + 8LL * (((unsigned int)((unsigned __int64)(a1[8] - a1[7]) >> 3) - 1) & a2)) = -1;
	    }
	}
	return old_WaterSplashEffect(a1, a2);
}
#endif

bool canDamageGeyser = true;
void (*old_TitleMessage_setSubtitle)(void *, std::string, void *);
void TitleMessage_setSubtitle(void *a1, std::string a2, void *a3) {
	if (canDamageGeyser) {
		if (useGeyserCooldown) {
		     if (a2 == oxorany("§8˙˙˙˙˙˙˙˙§7˙˙") || a2 == oxorany("§8˙˙˙˙˙˙˙˙˙§7˙") || a2 == oxorany("§8˙˙˙˙˙˙˙˙˙˙§7")) {
		        canKillAuraDamage = true;
		        canMobAuraDamage = true;
		        canDamageGeyser = false;
	        }
		}
	} else {
		canDamageGeyser = true;
	}
	return old_TitleMessage_setSubtitle(a1, a2, a3);
}

unsigned __int8 (*old_Block_getRenderLayer)(Block *thiz, void *a2, const vec3_ti &blockPos);
unsigned __int8 Block_getRenderLayer(Block *thiz, void *a2, const vec3_ti &blockPos) {
    if (inMenu) return old_Block_getRenderLayer(thiz, a2, blockPos);
	if (xrayDefault && !panic) {
    BlockType *v5 = thiz->getBlockTypeClassCustom();
    if (!v5) return old_Block_getRenderLayer(thiz, a2, blockPos);

    #if defined(__ARM_ARCH_7A__)
    const std::string *blockName = (const std::string *)((char *)v5 + 4);
    #else
    const std::string *blockName = (const std::string *)((char *)v5 + 8);
    #endif

    if (!blockName || blockName->empty()) return old_Block_getRenderLayer(thiz, a2, blockPos);

    std::string blockView = *blockName;
    vec3_t orePosition = {(float)blockPos.x, (float)blockPos.y, (float)blockPos.z};
    vec3_t camPos = {CameraPos.x, CameraPos.y, CameraPos.z};
    float distanceToBlock = distanceTo(&camPos, &orePosition);
    
    if (allowToXrayRender(blockView)) {
        if (distanceToBlock <= 32.0f) {
            bool alreadyExists = false;
            for (XrayOres existingOre : renderOresESP) {
                if (existingOre.orePos.x == blockPos.x && 
                    existingOre.orePos.y == blockPos.y && 
                    existingOre.orePos.z == blockPos.z) {
                    alreadyExists = true;
                    break;
                }
            }
            
            if (!alreadyExists) {
                renderOresESP.push_back({blockView, blockPos, {(float)blockPos.x, (float)blockPos.y, (float)blockPos.z}, GetOreColor(blockView)});
            }
        } 
    }
	}
    return old_Block_getRenderLayer(thiz, a2, blockPos);
}

void (*old_BaseActorRenderer_renderText)(void *a1, void *a2, void *a3, void *a4, void *a5, void *a6);
void BaseActorRenderer_renderText(void *a1, void *a2, void *a3, void *a4, void *a5, void *a6) {
	if (panic) return old_BaseActorRenderer_renderText(a1, a2, a3, a4, a5, a6);
	if (esp && espNametag && inHudScreen) {
		std::string currentNameTag = *static_cast<std::string*>(a3);
	    for (PlayerStruct player : nearPlayerList) {
			if (player.isBot) continue;
			if (currentNameTag.find(player.nickname) != std::string::npos || currentNameTag == player.nameTag) {
				return;
			}
	    }
	}
    return old_BaseActorRenderer_renderText(a1, a2, a3, a4, a5, a6);
}



#include "hotkeys.h"



pthread_t cheatThread;
bool isHackLoaded = false;
void *hack_thread(void *) {
    if (!isHackLoaded) {
        ProcMap mineMap;
        do {
            mineMap = KittyMemory::getLibraryMap(MineLib);
            handle = dlopen(OBFUSCATE("libminecraftpe.so"), RTLD_LAZY);
            if (!mineMap.isValid()) usleep(100000);
        } while (!mineMap.isValid());

        // All 1.21-specific hooks/patches were removed. They are not valid for
        // 1.26.60.4. Register your own hooks in OnMinecraftLoaded(handle).
        OnMinecraftLoaded(handle);

        isHackLoaded = true;
        return NULL;
    } else {
        pthread_kill(cheatThread, SIGUSR1);
    }
    return NULL;
}

JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM *vm, void *reserved) {
    JNIEnv *env;
    vm->GetEnv((void **) &env, JNI_VERSION_1_6);

    // Create a new thread so it does not block the main thread, means the game would not freeze
    pthread_create(&cheatThread, NULL, hack_thread, NULL);

    return JNI_VERSION_1_6;
}



JNIEXPORT void JNICALL
JNI_OnUnload(JavaVM *vm, void *reserved) {}

