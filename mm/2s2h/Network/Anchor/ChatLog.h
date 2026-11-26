#ifndef NETWORK_ANCHOR_CHATLOG_H
#define NETWORK_ANCHOR_CHATLOG_H
#ifdef __cplusplus

#include <deque>
#include <string>
#include <cstdint>
#include <chrono>
#include <vector>

// A highlighted range of ChatEntry::message (an @mention), in bytes
struct ChatHighlight {
    uint32_t start;
    uint32_t length;
};

struct ChatEntry {
    uint32_t clientId;
    std::string senderName;
    float colorR;
    float colorG;
    float colorB;
    std::string message;
    bool isSystem;
    std::chrono::system_clock::time_point timestamp;

    // @mentions
    std::vector<ChatHighlight> highlights;
    bool mentionsMe = false;
};

// Tab completion of player names in the chat input (like sm64coopdx).
// Tab after "@" (or "@" + the start of a name) completes "@Name ", and pressing Tab again cycles through the other
// matching names alphabetically (Shift+Tab goes backwards). Tab in an empty box cycles through everyone.
class ChatNameCompletion {
  public:
    // Edits buffer/cursor in place. Returns false if there was nothing to complete.
    bool Complete(std::string& buffer, int& cursor, const std::vector<std::string>& names, bool reverse);
    void Reset() { mActive = false; }

  private:
    bool mActive = false;
    std::string mPrefix;        // what was typed after "@" before the first Tab
    size_t mStart = 0;          // where the "@Name " being cycled starts
    size_t mInsertedLength = 0; // length of the text currently there (typed prefix, then each completion)
    int mIndex = -1;
    std::string mBufferAfter; // buffer/cursor right after our last completion, to detect "pressed Tab again"
    int mCursorAfter = -1;
};

class ChatLog {
  public:
    static constexpr size_t MAX_HISTORY = 2000;

    static void AddMessage(uint32_t clientId, const std::string& senderName, float r, float g, float b,
                            const std::string& message, const std::vector<ChatHighlight>& highlights = {},
                            bool mentionsMe = false);
    static void AddSystemMessage(const std::string& message);
    static const std::deque<ChatEntry>& GetMessages();
    static void Clear();

    // Finds "@Name" mentions of any of `names` (case-insensitive; the longest matching name wins, so names with
    // spaces work). Sets mentionsOwnName if one of them is `ownName`.
    static std::vector<ChatHighlight> FindMentions(const std::string& message, const std::vector<std::string>& names,
                                                   const std::string& ownName, bool& mentionsOwnName);

  private:
    static void Push(ChatEntry&& entry);

    static std::deque<ChatEntry> messages;
};

#endif
#endif
