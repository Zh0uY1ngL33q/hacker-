#pragma once
//
// LegitCriticals - legit (crouched) critical hits while airborne.
//

// Design: local-input driven (local-only), inherently network-consistent (no desync).
//   During the crit window (CRIT_WINDOW ticks): set local MoveInputComponent::mIsSneakDown (real crouch),
//   the game serializes crouch into PlayerAuthInput sent to server -- local and packet share the same
//   source, so it never desyncs.
//   After the window: cooldown (COOLDOWN ticks) where local is restored, no crouch -- you can run/jump normally.
//   This "crouch a few ticks -> silent a few ticks" rhythm guarantees you always have a window to jump, never locked.
//   Attack-driven: only an outbound attack packet (InventoryTransaction Attack) opens the window (airborne only),
//   so Aura / TriggerBot / manual attack all trigger it, and it only intervenes on attack (not global).
//   PushDown (optional, window-only): push fall speed to -0.1 to keep fallDistance>0 so apex/rising also crit.
//   (The old "only edit the PAI packet, no local move" version caused local/server state mismatch -> desync; abandoned.)
//

#include <Features/Modules/Module.hpp>
#include <Features/Modules/Setting.hpp>

class LegitCriticals : public ModuleBase<LegitCriticals> {
public:
    enum class Mode {
        Sneaking,       // crouch only (set sneak), leave sprint alone
        StopSprinting,  // stop sprint only (clear sprint), don't crouch
        Both            // crouch + stop sprint
    };

    EnumSettingT<Mode> mMode = EnumSettingT<Mode>(
        "Mode",
        "Airborne legit crits (local sneak -> serialized into PAI, no desync): Sneaking=crouch(crit); StopSprinting=stop sprint; Both=both",
        Mode::Sneaking,
        "Sneaking",
        "StopSprinting",
        "Both");

    // PushDown: during the crit window, each tick push vertical velocity to -0.1 (like z1yr's SetMotion(-0.1),
    // unconditional, applied while airborne). This is the crit source: at attack time the position is already
    // descending -> server sees fallDistance>0. Crouch is just disguise, it does not crit. On by default.
    BoolSetting mPushDown = BoolSetting(
        "PushDown",
        "While crit window, force fall speed to -0.1 every tick so fallDistance>0 at attack time (the actual crit source); sneak alone never crits",
        true);

    LegitCriticals() : ModuleBase("LegitCriticals", "Legit critical hits: crouch locally while airborne (consistent with PAI, no desync)", ModuleCategory::Combat, 0, false) {
        addSettings(&mMode, &mPushDown);

        mNames = {
            {Lowercase, "legitcriticals"},
            {LowercaseSpaced, "legitcriticals"},
            {Normal, "LegitCriticals"},
            {NormalSpaced, "LegitCriticals"}
        };
    }

    void onEnable() override;
    void onDisable() override;
    void onBaseTickEvent(class BaseTickEvent& event);
    void onPacketOutEvent(class PacketOutEvent& event);
    void applyInput(class PlayerAuthInputPacket& paip);

    // Attack-driven: any outbound attack packet opens a short window (airborne-only + cooldown ready).
    void arm();                   // airborne + cooldown ready -> open window; if already armed, refresh
    void disarm();                // restore local input, avoid residue

    std::string getSettingDisplay() override {
        return mMode.mValues[mMode.as<int>()];
    }

private:
    // Window 10 ticks: state takes effect one tick after the attack packet, so the first hit only "sows"
    // the window and crits land on later hits inside it (Aura combos / manual spam); too short a window
    // and later hits all fall outside the active state.
    static constexpr int CRIT_WINDOW  = 10; // crouch+push duration (ticks)
    static constexpr int COOLDOWN     = 3;  // silent cooldown (ticks): local restored, can run/jump
    bool mArmed   = false;          // crit window active? (only triggered by attack packets, not auto-global)
    int  mTtl     = 0;              // window remaining ticks; re-attacking inside refreshes
    int  mCooldown = 0;             // cooldown remaining ticks; >0 means local not crouching, run/jump window
};
