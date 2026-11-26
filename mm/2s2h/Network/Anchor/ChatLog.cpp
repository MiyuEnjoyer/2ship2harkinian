#include "ChatLog.h"

#include <algorithm>
#include <cctype>

std::deque<ChatEntry> ChatLog::messages;

// The chat window does its own line layout, so strip anything that would make ImGui
// start a new line on its own (newlines, tabs and other control characters).
static std::string SanitizeChatText(const std::string& text) {
    std::string out = text;
    for (char& c : out) {
        if (static_cast<unsigned char>(c) < 32) {
            c = ' ';
        }
    }
    return out;
}

void ChatLog::Push(ChatEntry&& entry) {
    messages.push_back(std::move(entry));

    while (messages.size() > MAX_HISTORY) {
        messages.pop_front();
    }
}

void ChatLog::AddMessage(uint32_t clientId, const std::string& senderName, float r, float g, float b,
                          const std::string& message, const std::vector<ChatHighlight>& highlights,
                          bool mentionsMe) {
    ChatEntry entry;
    entry.clientId   = clientId;
    entry.senderName = SanitizeChatText(senderName);
    entry.colorR     = r;
    entry.colorG     = g;
    entry.colorB     = b;
    entry.message    = SanitizeChatText(message);
    entry.isSystem   = false;
    entry.timestamp  = std::chrono::system_clock::now();
    entry.highlights = highlights; // sanitizing doesn't change lengths, so these still line up
    entry.mentionsMe = mentionsMe;

    Push(std::move(entry));
}

void ChatLog::AddSystemMessage(const std::string& message) {
    ChatEntry entry;
    entry.clientId   = 0;
    entry.senderName = "";
    entry.colorR     = 0.6f;
    entry.colorG     = 0.6f;
    entry.colorB     = 0.6f;
    entry.message    = SanitizeChatText(message);
    entry.isSystem   = true;
    entry.timestamp  = std::chrono::system_clock::now();

    Push(std::move(entry));
}

static bool IsNameChar(unsigned char c) {
    return std::isalnum(c) || c == '_' || c >= 0x80; // treat UTF-8 bytes as part of a name
}

static bool EqualsIgnoreCase(const std::string& text, size_t pos, const std::string& name) {
    if (pos + name.size() > text.size()) {
        return false;
    }
    for (size_t i = 0; i < name.size(); i++) {
        if (std::tolower((unsigned char)text[pos + i]) != std::tolower((unsigned char)name[i])) {
            return false;
        }
    }
    return true;
}

std::vector<ChatHighlight> ChatLog::FindMentions(const std::string& message, const std::vector<std::string>& names,
                                                 const std::string& ownName, bool& mentionsOwnName) {
    std::vector<ChatHighlight> result;
    mentionsOwnName = false;

    for (size_t i = 0; i < message.size(); i++) {
        if (message[i] != '@') {
            continue;
        }
        // "@" has to start a word (so e-mail addresses etc. don't count)
        if (i > 0 && IsNameChar((unsigned char)message[i - 1])) {
            continue;
        }

        const std::string* best = nullptr;
        for (const std::string& name : names) {
            if (name.empty() || (best != nullptr && name.size() <= best->size())) {
                continue;
            }
            const size_t end = i + 1 + name.size();
            // Must match the whole name, not the start of a longer word ("@Miyu" shouldn't match "@Miyuki")
            if (EqualsIgnoreCase(message, i + 1, name) &&
                (end >= message.size() || !IsNameChar((unsigned char)message[end]))) {
                best = &name;
            }
        }

        if (best != nullptr) {
            result.push_back({ (uint32_t)i, (uint32_t)(best->size() + 1) });
            if (!ownName.empty() && best->size() == ownName.size() && EqualsIgnoreCase(*best, 0, ownName)) {
                mentionsOwnName = true;
            }
            i += best->size();
        }
    }

    return result;
}

const std::deque<ChatEntry>& ChatLog::GetMessages() {
    return messages;
}

void ChatLog::Clear() {
    messages.clear();
}


// MARK: - Name completion

static std::string ToLower(const std::string& text) {
    std::string out = text;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return out;
}

static bool StartsWithIgnoreCase(const std::string& text, const std::string& prefix) {
    return prefix.size() <= text.size() && EqualsIgnoreCase(text, 0, prefix);
}

bool ChatNameCompletion::Complete(std::string& buffer, int& cursorPos, const std::vector<std::string>& allNames,
                                  bool reverse) {
    const size_t cursor = (size_t)std::clamp(cursorPos, 0, (int)buffer.size());
    const bool continuing = mActive && buffer == mBufferAfter && (int)cursor == mCursorAfter;

    if (!continuing) {
        mActive = false;

        // Find an "@" (at the start of a word) whose text up to the cursor is the start of someone's name.
        // Search backwards so names with spaces ("@Link F") still work.
        bool found = false;
        for (size_t at = cursor; at-- > 0;) {
            if (buffer[at] != '@' || (at > 0 && buffer[at - 1] != ' ')) {
                continue;
            }
            const std::string typed = buffer.substr(at + 1, cursor - at - 1);
            const bool matchesSomeone = std::any_of(allNames.begin(), allNames.end(), [&](const std::string& name) {
                return !name.empty() && StartsWithIgnoreCase(name, typed);
            });
            if (matchesSomeone) {
                mPrefix = typed;
                mStart = at;
                mInsertedLength = cursor - at;
                found = true;
            }
            break; // only the nearest "@" counts
        }

        // Empty box: cycle through everyone
        if (!found && buffer.find_first_not_of(' ') == std::string::npos) {
            buffer.clear();
            mPrefix = "";
            mStart = 0;
            mInsertedLength = 0;
            found = true;
        }

        if (!found) {
            return false;
        }
        mIndex = -1;
    }

    // Matching names, alphabetical (case-insensitive), no duplicates
    std::vector<std::string> names;
    for (const std::string& name : allNames) {
        if (!name.empty() && StartsWithIgnoreCase(name, mPrefix) &&
            std::find(names.begin(), names.end(), name) == names.end()) {
            names.push_back(name);
        }
    }
    if (names.empty()) {
        mActive = false;
        return false;
    }
    std::sort(names.begin(), names.end(),
              [](const std::string& a, const std::string& b) { return ToLower(a) < ToLower(b); });

    const int count = (int)names.size();
    if (mIndex < 0) {
        mIndex = reverse ? count - 1 : 0;
    } else {
        mIndex = (mIndex + (reverse ? count - 1 : 1)) % count;
    }

    // Add a space after the name, unless one is already there (completing in the middle of a message)
    const size_t afterEnd = mStart + mInsertedLength;
    const bool spaceFollows = afterEnd < buffer.size() && buffer[afterEnd] == ' ';
    const std::string replacement = "@" + names[mIndex] + (spaceFollows ? "" : " ");
    buffer.replace(mStart, mInsertedLength, replacement);
    mInsertedLength = replacement.size();

    cursorPos = (int)(mStart + replacement.size() + (spaceFollows ? 1 : 0));
    mActive = true;
    mBufferAfter = buffer;
    mCursorAfter = cursorPos;
    return true;
}
