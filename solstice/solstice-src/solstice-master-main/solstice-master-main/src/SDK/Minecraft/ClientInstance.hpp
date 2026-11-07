#pragma once

#include <Utils/MemUtils.hpp>

#include <Utils/Structs.hpp>
//
// Created by vastrakai on 6/24/2024.
//



class ClientInstance {
public:
    //CLASS_FIELD(class MinecraftGame*, mcGame, 0xC8);

    static ClientInstance* get();



    class MinecraftSim* getMinecraftSim();
    class LevelRenderer* getLevelRenderer();
    class LoopbackPacketSender* getPacketSender();
    class GuiData* getGuiData();
    class bgfx_context* getBGFX();
    glmatrixf getViewMatrix();
    glm::vec2 getFov();
    // The two floats exactly as stored at the FOV members, before any unit
    // interpretation. Diagnostics only: the projection must keep using getFov(),
    // which normalises them. Logged once by the ESP probe so the stored unit can
    // be settled from a log instead of guessed.
    glm::vec2 getFovRaw();

    class ClientInputHandler* getInputHandler();
    class KeyboardMouseSettings* getKeyboardSettings();
    class MinecraftGame* getMinecraftGame();
    class Actor* getLocalPlayer();
    class BlockSource* getBlockSource();
    class Options* getOptions();
    std::string getScreenName();
    void setDisableInput(bool disable);
    bool getMouseGrabbed();
    void grabMouse();
    void releaseMouse();
    void playUi(const std::string& soundName, float volume, float pitch);
};