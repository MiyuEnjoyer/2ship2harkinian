#include "Anchor.h"
#include "ChatLog.h"
#include <libultraship/bridge/controllerbridge.h>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <string>
#include <vector>

#define ANCHOR_CHAT_GAME_INPUT_BLOCK_ID 8675309

int AnchorChatWindow::InputTextCallback(ImGuiInputTextCallbackData* data) {
    AnchorChatWindow* self = static_cast<AnchorChatWindow*>(data->UserData);
    if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
        self->mNameCompletion.Reset();
        self->HandleHistoryNavigation(data, data->EventKey == ImGuiKey_UpArrow);
    } else if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
        self->HandleNameCompletion(data);
    }
    return 0;
}

void AnchorChatWindow::HandleNameCompletion(ImGuiInputTextCallbackData* data) {
    if (Anchor::Instance == nullptr) {
        return;
    }

    // Everyone in the room except yourself
    const std::string ownName = CVarGetString(CVAR_REMOTE_ANCHOR("Name"), "");
    std::vector<std::string> names;
    for (const std::string& name : Anchor::Instance->GetMentionableNames()) {
        if (name != ownName) {
            names.push_back(name);
        }
    }

    std::string buffer(data->Buf, data->BufTextLen);
    int cursor = data->CursorPos;
    const bool reverse = ImGui::GetIO().KeyShift;
    if (!mNameCompletion.Complete(buffer, cursor, names, reverse)) {
        return;
    }
    if ((int)buffer.size() >= data->BufSize) {
        return; // wouldn't fit in the input box
    }

    data->DeleteChars(0, data->BufTextLen);
    data->InsertChars(0, buffer.c_str());
    data->CursorPos = cursor;
    data->SelectionStart = data->SelectionEnd = cursor;
}

void AnchorChatWindow::HandleHistoryNavigation(ImGuiInputTextCallbackData* data, bool up) {
    if (mSentHistory.empty()) {
        return;
    }

    if (up) {
        if (mHistoryPos == -1) {
            mHistoryPos = static_cast<int32_t>(mSentHistory.size()) - 1;
        } else if (mHistoryPos > 0) {
            mHistoryPos--;
        }
    } else {
        if (mHistoryPos != -1) {
            mHistoryPos++;
            if (mHistoryPos >= static_cast<int32_t>(mSentHistory.size())) {
                mHistoryPos = -1;
            }
        }
    }

    const std::string replacement = (mHistoryPos == -1) ? "" : mSentHistory[mHistoryPos];
    data->DeleteChars(0, data->BufTextLen);
    data->InsertChars(0, replacement.c_str());
}

void AnchorChatWindow::Draw() {
    if (CVarGetInteger(CVAR_WINDOW("AnchorChat"), 0) && !IsVisible()) {
        Show();
    }

    if (mCaptureFrames > 0) {
        mCaptureFrames--;
        if (mCaptureFrames == 0) {
            // genuinely enough to stop game from registering game inputs between chat messages yes it's stupid
            ControllerUnblockGameInput(ANCHOR_CHAT_GAME_INPUT_BLOCK_ID);
        }
    }
    if (Anchor::Instance && Anchor::Instance->isConnected) {
        const int hotkeyChoice = CVarGetInteger(CVAR_REMOTE_ANCHOR("ChatHotkey"), 0);
        const ImGuiKey key = (hotkeyChoice == 1) ? ImGuiKey_T : ImGuiKey_Enter;

        if (ImGui::IsKeyPressed(key, false)) {
            const bool inputFocused = ImGui::GetIO().WantTextInput;
            const bool bufferEmpty = (mInputBuffer[0] == '\0');

            if (IsVisible() && inputFocused && bufferEmpty) {
                ImGui::SetWindowFocus(nullptr);
                mRequestFocus = false;
                mCaptureFrames = 1;
                ControllerBlockGameInput(ANCHOR_CHAT_GAME_INPUT_BLOCK_ID);
            } else if (!inputFocused) {
                if (!IsVisible()) {
                    Show();
                    CVarSetInteger(CVAR_WINDOW("AnchorChat"), 1);
                    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
                }
                mRequestFocus = true;
                mCaptureFrames = 1;
                ControllerBlockGameInput(ANCHOR_CHAT_GAME_INPUT_BLOCK_ID);
            }
        }
    }

    if (!IsVisible()) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(420, 320), ImGuiCond_FirstUseEver);

    bool open = true;
    if (ImGui::Begin("Anchor Chat", &open, ImGuiWindowFlags_NoFocusOnAppearing)) {
        DrawElement();
    }
    ImGui::End();

    if (!open) {
        Hide();
        CVarSetInteger(CVAR_WINDOW("AnchorChat"), 0);
        Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
}

// MARK: - Chat line layout

namespace {

struct ChatTextSpan {
    const char* begin;
    const char* end;
    ImU32 color;
};

bool IsWrapBlank(char c) {
    return c == ' ' || c == '\t';
}

// True when ImGui's word wrap had to cut a word in half because it didn't fit in the space left on the line.
bool IsCutMidWord(const char* start, const char* eol, const char* end) {
    if (eol <= start || eol >= end) {
        return false;
    }
    const char prev = eol[-1];
    const bool prevIsPunct =
        prev == '.' || prev == ',' || prev == ';' || prev == '!' || prev == '?' || prev == '"';
    return !IsWrapBlank(prev) && !IsWrapBlank(*eol) && !prevIsPunct;
}

// Draws a run of differently colored text pieces as one paragraph. Wrapped lines go back to the left
// edge of the chat (instead of lining up under wherever the message text started, which is what
// SameLine + TextWrapped does when the sender's name is long).
void DrawChatSpans(const ChatTextSpan* spans, size_t count, ImU32 backgroundColor = 0) {
    ImFont* font = ImGui::GetFont();
    const float fontSize = ImGui::GetFontSize();
    const float scale = fontSize / font->FontSize;
    const float lineHeight = ImGui::GetTextLineHeight();
    const float maxWidth = std::max(ImGui::GetContentRegionAvail().x, fontSize);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Text goes on top (channel 1), the highlight behind it (channel 0), since its height is only known at the end
    if (backgroundColor != 0) {
        drawList->ChannelsSplit(2);
        drawList->ChannelsSetCurrent(1);
    }

    float x = 0.0f;
    float y = 0.0f;
    bool atLineStart = true;

    for (size_t i = 0; i < count; i++) {
        const char* s = spans[i].begin;
        const char* end = spans[i].end;

        while (s < end) {
            if (atLineStart) {
                while (s < end && IsWrapBlank(*s)) {
                    s++;
                }
                if (s >= end) {
                    break;
                }
            }

            const char* eol = font->CalcWordWrapPositionA(scale, s, end, maxWidth - x);

            // The next word doesn't fit on what's left of this line: move it down to the next one
            // (only cut words that are too long for a whole line on their own)
            if (!atLineStart && (eol <= s || IsCutMidWord(s, eol, end))) {
                x = 0.0f;
                y += lineHeight;
                atLineStart = true;
                continue;
            }
            if (eol <= s) {
                eol = s + 1;
            }

            drawList->AddText(font, fontSize, ImVec2(origin.x + x, origin.y + y), spans[i].color, s, eol);
            x += font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, s, eol).x;
            atLineStart = false;
            s = eol;

            if (s < end) {
                x = 0.0f;
                y += lineHeight;
                atLineStart = true;
            }
        }
    }

    if (backgroundColor != 0) {
        drawList->ChannelsSetCurrent(0);
        const ImVec2 min(origin.x - 2.0f, origin.y);
        const ImVec2 max(origin.x + maxWidth, origin.y + y + lineHeight);
        drawList->AddRectFilled(min, max, backgroundColor, 2.0f);
        drawList->AddRectFilled(min, ImVec2(min.x + 2.0f, max.y), (backgroundColor | IM_COL32_A_MASK));
        drawList->ChannelsMerge();
    }

    ImGui::Dummy(ImVec2(maxWidth, y + lineHeight));
}

// Same "greentext" colors as the coop chat mod
ImU32 ChatBodyColor(const std::string& message, ImU32 defaultColor) {
    if (!message.empty() && message[0] == '>') {
        return IM_COL32(0, 255, 0, 255);
    }
    if (!message.empty() && message[0] == '<') {
        return IM_COL32(255, 0, 255, 255);
    }
    return defaultColor;
}

constexpr ImU32 MENTION_COLOR = IM_COL32(255, 255, 0, 255);
constexpr ImU32 MENTION_BACKGROUND = IM_COL32(255, 255, 0, 40);

void ToggleChatCVar(const char* cvar, int32_t defaultValue) {
    CVarSetInteger(cvar, CVarGetInteger(cvar, defaultValue) ? 0 : 1);
    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
}

} // namespace

void AnchorChatWindow::DrawElement() {
    const float footerHeight = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();

    ImGui::BeginChild("AnchorChatScrollback", ImVec2(0, -footerHeight), true);

    const bool showTimestamps = CVarGetInteger(CVAR_REMOTE_ANCHOR("ChatShowTimestamps"), 0) != 0;

    const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 disabledColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);

    for (const ChatEntry& entry : ChatLog::GetMessages()) {
        std::vector<ChatTextSpan> spans;
        auto addSpan = [&](const char* begin, const char* end, ImU32 color) {
            if (begin < end) {
                spans.push_back({ begin, end, color });
            }
        };
        auto addString = [&](const std::string& text, ImU32 color) {
            addSpan(text.c_str(), text.c_str() + text.size(), color);
        };

        std::string timestamp;
        if (showTimestamps) {
            auto time_t = std::chrono::system_clock::to_time_t(entry.timestamp);
            std::tm tm = *std::localtime(&time_t);

            char buf[16];
            std::strftime(buf, sizeof(buf), "[%H:%M] ", &tm);
            timestamp = buf;
            addString(timestamp, disabledColor);
        }

        const ImU32 entryColor = ImGui::GetColorU32(ImVec4(entry.colorR, entry.colorG, entry.colorB, 1.0f));

        std::string prefix;
        std::string body;
        if (entry.isSystem) {
            body = "* " + entry.message;
            addString(body, entryColor);
        } else {
            prefix = entry.senderName + ":";
            addString(prefix, entryColor);

            // Message text, with each @mention in yellow
            const ImU32 bodyColor = ChatBodyColor(entry.message, textColor);
            body = " " + entry.message;
            const char* text = body.c_str() + 1; // entry.message, inside body
            const char* textEnd = body.c_str() + body.size();
            const char* cursor = body.c_str();
            for (const ChatHighlight& h : entry.highlights) {
                const char* hStart = text + h.start;
                const char* hEnd = hStart + h.length;
                if (hStart < cursor || hEnd > textEnd) {
                    continue; // shouldn't happen, but never draw out of bounds
                }
                addSpan(cursor, hStart, bodyColor);
                addSpan(hStart, hEnd, MENTION_COLOR);
                cursor = hEnd;
            }
            addSpan(cursor, textEnd, bodyColor);
        }

        DrawChatSpans(spans.data(), spans.size(), entry.mentionsMe ? MENTION_BACKGROUND : 0);
    }

    if (ImGui::BeginPopupContextWindow("AnchorChatContextMenu")) {
        if (ImGui::MenuItem("Show Timestamps", nullptr, showTimestamps)) {
            ToggleChatCVar(CVAR_REMOTE_ANCHOR("ChatShowTimestamps"), 0);
        }
        ImGui::EndPopup();
    }

    const bool wasAtBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;
    if (wasAtBottom) {
        ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();
    ImGui::Separator();

    const ImGuiInputTextFlags inputFlags =
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory |
        ImGuiInputTextFlags_CallbackCompletion;

    if (mRequestFocus) {
        ImGui::SetKeyboardFocusHere(0);
        mRequestFocus = false;
    }

    ImGui::SetNextItemWidth(-1);
    const bool submitted = ImGui::InputText("##AnchorChatInput", mInputBuffer, IM_ARRAYSIZE(mInputBuffer),
    inputFlags, &AnchorChatWindow::InputTextCallback, this);

    if (submitted) {
        std::string message(mInputBuffer);

        if (!message.empty() && Anchor::Instance->isConnected) {
            Anchor::Instance->SendPacket_ChatMessage(message);
            mSentHistory.push_back(message);
            mHistoryPos = -1;
        }

        mInputBuffer[0] = '\0';
        mNameCompletion.Reset();
        mRequestFocus = true;
        mCaptureFrames = 1;
        ControllerBlockGameInput(ANCHOR_CHAT_GAME_INPUT_BLOCK_ID);
    }

    if (ImGui::IsItemActive() || submitted) {
        ImGui::GetIO().WantCaptureKeyboard = true;
    }
}