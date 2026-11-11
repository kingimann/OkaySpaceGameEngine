"""Gameplay library, batch 6: dialogue. They drive the engine's Dialogue Runner. Called by gen_gameplay_library.py."""


def define(add, N, B, T, O, R):
    D = ("Scene", "Dialogue")
    talk = ("me->AddComponent<DialogueRunner>(); auto* dr = me->GetComponent<DialogueRunner>(); dr->drawUI = false; dr->typewriterSpeed = 0.0f; dr->SetGraph(TalkGraph()); ")
    start = "me->GetComponent<DialogueRunner>()->StartDialogue();"
    choice = "me->GetComponent<DialogueRunner>()->StartDialogue(); me->GetComponent<DialogueRunner>()->Advance();"
    add("GlIsDialogueRunning", "Is Dialogue Running", *D, "Basic", "True while a conversation is going on.",
        [O()], [B("Result")], "DialogueRunner* r = GlDialogue(this, o, g, ctx); out = VsValue(r && r->IsActive());",
        reg="CAT_LOGIC", setup=talk, post=start, update=True, expect=True, etype="b", demo="dialogue")
    add("GlGetDialogueLine", "Get Dialogue Line", *D, "Basic", "The words being spoken now. Empty when no conversation is going.",
        [O()], [T("Text")], "DialogueRunner* r = GlDialogue(this, o, g, ctx); out = VsValue(r ? r->CurrentLine().text : std::string());",
        setup=talk, post=start, update=True, expect="Hello there", etype="s", demo="dialogue")
    add("GlGetDialogueSpeaker", "Get Dialogue Speaker", *D, "Basic", "Who is speaking now.",
        [O()], [T("Name")], "DialogueRunner* r = GlDialogue(this, o, g, ctx); out = VsValue(r ? r->CurrentLine().speaker : std::string());",
        setup=talk, post=start, update=True, expect="Smith", etype="s")
    add("GlIsDialogueWaitingForChoice", "Is Dialogue Waiting for Choice", *D, "Intermediate", "True when the conversation is showing options and waits for one to be picked.",
        [O()], [B("Result")], "DialogueRunner* r = GlDialogue(this, o, g, ctx); out = VsValue(r && r->IsActive() && r->CurrentLine().choice);",
        reg="CAT_LOGIC", setup=talk, post=choice, update=True, expect=True, etype="b")
    add("GlGetDialogueOptionCount", "Get Dialogue Option Count", *D, "Intermediate", "How many options the player can pick from now.",
        [O()], [N("Count")], "DialogueRunner* r = GlDialogue(this, o, g, ctx); out = VsValue(r ? (float)r->CurrentLine().choices.size() : 0.0f);",
        setup=talk, post=choice, update=True, expect=2.0)
    add("GlGetDialogueOptionText", "Get Dialogue Option Text", *D, "Intermediate", "The words of one option, counted from 0.",
        [N("Option"), O()], [T("Text")],
        "DialogueRunner* r = GlDialogue(this, o, g, ctx); std::string t; if (r) { auto ch = r->CurrentLine().choices; int i = (int)std::lround(n0); if (i >= 0 && i < (int)ch.size()) t = ch[(size_t)i]; } out = VsValue(t);",
        defaults=["1", None], setup=talk, post=choice, update=True, expect="No thanks", etype="s")
    add("GlAdvanceDialogue", "Advance Dialogue", *D, "Basic", "Moves the conversation on to the next line.",
        [O()], [], "if (DialogueRunner* r = GlDialogue(this, o, g, ctx)) r->Advance();",
        reg="CAT_ACTION", exec_=True, setup=talk, post=start, update=True,
        check="CHECK(me->GetComponent<DialogueRunner>()->CurrentLine().choice);", demo="dialogue")
    add("GlChooseDialogueOption", "Choose Dialogue Option", *D, "Intermediate", "Picks one of the options, counted from 0, and carries the conversation on.",
        [N("Option"), O()], [], "if (DialogueRunner* r = GlDialogue(this, o, g, ctx)) r->Choose(std::max(0, (int)std::lround(n0)));",
        reg="CAT_ACTION", exec_=True, defaults=["0", None], setup=talk, post=choice, update=True,
        check='CHECK(me->GetComponent<DialogueRunner>()->CurrentLine().text == "Glad to help");')
    add("GlStopDialogue", "Stop Dialogue", *D, "Basic", "Ends the conversation now.",
        [O()], [], "if (DialogueRunner* r = GlDialogue(this, o, g, ctx)) r->Stop();",
        reg="CAT_ACTION", exec_=True, setup=talk, post=start, update=True, check="CHECK(!me->GetComponent<DialogueRunner>()->IsActive());")
