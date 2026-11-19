# Number-key dialogue responses (test build)

Engine feature; no plugin or Lua mod is needed. Install the matching engine build.
Existing mouse and controller selection remain available.

The currently displayed response choices in the conversation text are labelled
1 through 9, followed by 0 for the tenth. Press the matching number-row or numpad
key to select that response. Extra choices remain mouse/controller-selectable.
This does not number the topic/service sidebar, persuasion buttons or standalone
message boxes. Response order and original script choice IDs are preserved.

The shortcut is active only with native dialogue on top, no modal prompt, and no
text-input focus. Ctrl/Alt/Shift/Windows-key combinations are left alone. Key
repeats are consumed without selecting another response; stale choices are not
activated before they have been displayed. Goodbye is not assigned a number.

Offline regression: the actual response handler is compiled with minimal mocks
and exercises all digit mappings, arbitrary script IDs, repeats across choice
sets, out-of-range digits, stale data, goodbye, hidden dialogue and missing actor.
Full engine compilation and live-game validation are separate gates.

In-game checklist:
1. Open a conversation offering at least two response choices; press 2. Confirm
   the second displayed answer is chosen and its usual quest result occurs.
2. Repeat with numpad 1. Check clicking the responses still works.
3. Hold 1 where an answer leads to another set of responses. Only the first set
   should be answered; release and press again to answer the next set.
4. Try an unavailable digit and modified digits. No answer should be selected.
5. Open the console, a text input, or a modal prompt above dialogue. Digits must
   not select the background conversation.
6. Check a mod conversation with nonsequential Choice IDs and, if available,
   ten or more answers: 0 selects response ten, not script ID zero.
7. Check topic/service clicks and controller navigation remain unchanged.

Also includes a follow-up startup-screen build repair: Windows drawing is now
compiled in startupscreen.cpp with GDI enabled before the first Windows include.
The public header contains no Win32 drawing code. The Windows smoke gate compiles
with the engine's NOGDI/NOMB/NOMINMAX/WIN32_LEAN_AND_MEAN flags.
