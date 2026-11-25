#pragma once
// Emoji, chat text formatting and link previews (ChatText.cpp): the emoji tokens and their drawings, the Discord-style
// markup tokenizer that draws and measures a chat body, and the link-preview cards. Moved out of launcher/main.cpp
// unchanged (docs/engine/modularization_plan.md, Part C section 5, batch L1).
#include "imgui.h"
#include <cstddef>
#include <string>

namespace okl {

// ---- Emoji -----------------------------------------------------------------
// The shipped font has no emoji glyphs, so emoji are small vector drawings keyed
// by text shortcodes (Discord/Slack style). The composer's emoji picker inserts
// a shortcode; RenderChatBody draws it inline. This renders identically on every
// machine with no emoji font to ship.
struct EmojiDef { const char* code; const char* alt; int id; };
static const EmojiDef kEmojis[] = {
    {":)",     ":smile:",    0}, {":D",    ":grin:",  1}, {":(",     ":sad:",   2},
    {":heart:","<3",         3}, {":fire:", ":flame:", 4}, {":star:", ":+1:",   5},
    {":laugh:","xD",          6}, {":wow:", ":o",      7}, {":cool:", ":sunglasses:", 8},
    {":thumbsup:",":+1b:",   9},
};
static constexpr int kEmojiUnknown = 99;   // a Unicode emoji we have no drawing for
static constexpr int kEmojiUnicode = 98;   // "<e:1F600>" — a real emoji, drawn from the Twemoji sheet
static constexpr int kEmojiCustom  = 97;   // "<:name:url>" — a user's custom emoji image

// The quick-reaction palette (Messenger/Telegram-style): a short key stored in
// message_reactions.emoji, paired with the drawable emoji id it renders as.
struct ReactionDef { const char* key; int id; };
static const ReactionDef kReactions[] = {
    {"heart", 3}, {"thumbsup", 9}, {"laugh", 6}, {"wow", 7}, {"sad", 2}, {"fire", 4},
};

// What main.cpp uses from ChatText.cpp.
int EmojiId(const std::string& t);                               // the drawable id of an emoji token, -1 = not an emoji
std::string Utf8Encode(unsigned cp);
std::string NormalizeEmoji(const std::string& in);               // Unicode emoji clusters -> "<e:...>" tokens
int ReactionEmojiId(const std::string& key);
bool IsEmojiOnly(const std::string& bodyIn, int& count);         // 1-3 emoji and nothing else (drawn big)
void DrawEmoji(ImDrawList* dl, ImVec2 c, float s, int id);       // emoji `id` centred at c in a box of side s
void DrawHeart(ImDrawList* dl, ImVec2 c, float r, ImU32 col);
void EmojiPicker(const char* id, char* buf, std::size_t cap);    // emoji button + picker for any text box
extern ImFont* g_fontMono;                                       // JetBrains Mono for `code` (loaded in main)
void MergeEmojiFallback(float size);                             // merge the system emoji font into the font just added
void RenderChatBody(const std::string& bodyIn, float maxW = 0.0f);   // a message body with markup, emoji and link cards
float MeasureChatBodyWidth(const std::string& bodyIn, float maxW);
void IconTile(const std::string& name, float size);              // a coloured tile with the name's initial
void DrawTemplateStage(ImDrawList* d, const std::string& title, ImVec2 a, ImVec2 b);
unsigned char* LoadRgbaFile(const std::string& path, int* w, int* h);   // RGBA8, free with std::free; nullptr on failure
} // namespace okl
