#include "2s2h/Network/Anchor/Anchor.h"
#include "2s2h/Network/Anchor/ChatLog.h"
#include "2s2h/BenGui/Notification.h"
#include <nlohmann/json.hpp>

extern "C" {
#include "variables.h"
#include "functions.h"
#include "sfx.h"
}

namespace BenGui {
extern std::shared_ptr<AnchorChatWindow> mAnchorChatWindow;
}

/**
 * CHAT_MESSAGE
 */

static void PlaySystemSfx(u16 sfxId) {
    AudioSfx_PlaySfx(sfxId, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                     &gSfxDefaultReverb);
}

// Played for every chat message
const char* const ANCHOR_CHAT_SFX_NAMES[] = {
    "Message Pass", "Cursor Move", "Cursor Decide", "File Cursor", "Enter File", "Tambourine",
};
static const u16 CHAT_SFX_IDS[] = {
    NA_SE_SY_MESSAGE_PASS, NA_SE_SY_CURSOR,        NA_SE_SY_DECIDE,
    NA_SE_SY_FSEL_CURSOR,  NA_SE_SY_FSEL_DECIDE_L, NA_SE_SY_METRONOME,
};
const int ANCHOR_CHAT_SFX_COUNT = (int)(sizeof(CHAT_SFX_IDS) / sizeof(CHAT_SFX_IDS[0]));

void AnchorChat_PlayChatSfx() {
    int index = CVarGetInteger(CVAR_REMOTE_ANCHOR("ChatSfx"), 0);
    if (index < 0 || index >= ANCHOR_CHAT_SFX_COUNT) {
        index = 0;
    }
    PlaySystemSfx(CHAT_SFX_IDS[index]);
}

// Played instead of the normal chat sound when someone @mentions you
const char* const ANCHOR_CHAT_PING_SFX_NAMES[] = {
    "Tatl (Call)",
    "Secret Chime",
    "Rupee",
    "Attention",
};
static const u16 PING_SFX_IDS[] = {
    NA_SE_VO_NAVY_CALL,
    NA_SE_SY_CORRECT_CHIME,
    NA_SE_SY_GET_RUPY,
    NA_SE_SY_ATTENTION_ON,
};
const int ANCHOR_CHAT_PING_SFX_COUNT = (int)(sizeof(PING_SFX_IDS) / sizeof(PING_SFX_IDS[0]));

void AnchorChat_PlayPingSfx() {
    int index = CVarGetInteger(CVAR_REMOTE_ANCHOR("ChatPingSound"), 0);
    if (index < 0 || index >= ANCHOR_CHAT_PING_SFX_COUNT) {
        index = 0;
    }
    PlaySystemSfx(PING_SFX_IDS[index]);
}

void Anchor::SendPacket_ChatMessage(const std::string& message) {
    if (!isConnected || message.empty()) {
        return;
    }

    nlohmann::json payload;
    payload["type"] = CHAT_MESSAGE;
    payload["message"] = message;
    QueueOutgoingPacket(payload);

    const std::string ownName = CVarGetString(CVAR_REMOTE_ANCHOR("Name"), "");

    float r = 0.8f, g = 1.0f, b = 0.8f;
    auto ownIt = clients.find(ownClientId);
    if (ownIt != clients.end()) {
        r = ownIt->second.color.r / 255.0f;
        g = ownIt->second.color.g / 255.0f;
        b = ownIt->second.color.b / 255.0f;
    }

    bool mentionsMe = false; // pinging yourself doesn't ping
    const auto highlights = ChatLog::FindMentions(message, GetMentionableNames(), "", mentionsMe);
    ChatLog::AddMessage(ownClientId, ownName, r, g, b, message, highlights);

    AnchorChat_PlayChatSfx();
}

std::vector<std::string> Anchor::GetMentionableNames() {
    std::vector<std::string> names;
    for (auto& [clientId, client] : clients) {
        if (client.online && !client.name.empty()) {
            names.push_back(client.name);
        }
    }
    names.push_back(CVarGetString(CVAR_REMOTE_ANCHOR("Name"), ""));
    return names;
}

void Anchor::HandlePacket_ChatMessage(nlohmann::json payload) {
    const uint32_t clientId = payload.at("clientId").get<uint32_t>();
    const std::string message = payload.at("message").get<std::string>();

    std::string senderName = "Unknown";
    float r = 1.0f, g = 1.0f, b = 1.0f;

    auto it = clients.find(clientId);
    if (it != clients.end()) {
        senderName = it->second.name;
        r = it->second.color.r / 255.0f;
        g = it->second.color.g / 255.0f;
        b = it->second.color.b / 255.0f;
    }

    bool mentionsMe = false;
    const auto highlights = ChatLog::FindMentions(message, GetMentionableNames(),
                                                  CVarGetString(CVAR_REMOTE_ANCHOR("Name"), ""), mentionsMe);
    ChatLog::AddMessage(clientId, senderName, r, g, b, message, highlights, mentionsMe);

    if (!mentionsMe) {
        AnchorChat_PlayChatSfx();
        return;
    }

    AnchorChat_PlayPingSfx();

    // With the chat window closed, a ping would be easy to miss, so pop it up as a notification too
    const bool chatOpen = BenGui::mAnchorChatWindow != nullptr && BenGui::mAnchorChatWindow->IsVisible();
    if (!chatOpen && CVarGetInteger(CVAR_REMOTE_ANCHOR("ChatPingPopups"), 1)) {
        Notification::Emit({
            .prefix = senderName + ":",
            .prefixColor = ImVec4(r, g, b, 1.0f),
            .message = message,
            .messageColor = ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
            .mute = true, // the ping sound already played
        });
    }
}
