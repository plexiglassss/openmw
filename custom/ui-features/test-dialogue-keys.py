"""Compile the production response-key handler against minimal UI mocks."""
import pathlib
import subprocess
import tempfile

source = pathlib.Path('apps/openmw/mwgui/dialogue.cpp').read_text()
start = source.index('    bool DialogueWindow::onChoiceKeyPress(')
end = source.index('    void DialogueWindow::onChoiceActivated(', start)
method = source[start:end]
fixture = r'''
#include <cassert>
#include <string>
#include <utility>
#include <vector>
namespace MyGUI {
struct KeyCode {
enum Enum { One, Two, Three, Four, Five, Six, Seven, Eight, Nine, Zero,
Numpad1, Numpad2, Numpad3, Numpad4, Numpad5, Numpad6, Numpad7, Numpad8, Numpad9, Numpad0, Space };
Enum value;
KeyCode(Enum v): value(v) {}
Enum getValue() const { return value; }
};
}
namespace ESM { struct RefId { static int stringRefId(const char*) { return 0; } }; }
using Choices = std::vector<std::pair<std::string, int>>;
struct Manager {
    Choices choices;
    bool goodbye = false;
    const Choices& getChoices() const { return choices; }
    bool isGoodbye() const { return goodbye; }
};
struct Windows { int sounds = 0; void playSound(int) { ++sounds; } };
namespace MWBase { struct Environment {
    Manager manager; Windows windows;
    static Environment& get() { static Environment e; return e; }
    struct NotNull { Manager* p; Manager* operator->() const { return p; } };
    NotNull getDialogueManager() { return {&manager}; }
    Windows* getWindowManager() { return &windows; }
}; }
struct DialogueWindow {
    struct Ptr { bool empty = false; bool isEmpty() const { return empty; } } mPtr;
    bool visible = true, mGoodbye = false;
    Choices mChoices;
    int answer = -1, calls = 0;
    bool isVisible() const { return visible; }
    void onChoiceActivated(int id) { answer = id; ++calls; }
    bool onChoiceKeyPress(MyGUI::KeyCode, bool);
};
'''
checks = r'''
int main() {
    using K = MyGUI::KeyCode;
    auto& env = MWBase::Environment::get();
    DialogueWindow d;
    for (int i = 0; i < 11; ++i) d.mChoices.emplace_back("answer", 100 + i * 7);
    env.manager.choices = d.mChoices;
    const K::Enum row[] = {K::One,K::Two,K::Three,K::Four,K::Five,K::Six,K::Seven,K::Eight,K::Nine,K::Zero};
    const K::Enum pad[] = {K::Numpad1,K::Numpad2,K::Numpad3,K::Numpad4,K::Numpad5,K::Numpad6,K::Numpad7,K::Numpad8,K::Numpad9,K::Numpad0};
    for (int i = 0; i < 10; ++i) {
        assert(d.onChoiceKeyPress(row[i], false) && d.answer == 100 + i * 7);
        assert(d.onChoiceKeyPress(pad[i], false) && d.answer == 100 + i * 7);
    }
    assert(d.calls == 20 && env.windows.sounds == 20);
    assert(!d.onChoiceKeyPress(K::Space, false));
    assert(d.onChoiceKeyPress(K::One, true) && d.calls == 20);
    // A held key must not select a response in a newly opened choice set.
    d.mChoices = {{"next", 567}}; env.manager.choices = d.mChoices;
    assert(d.onChoiceKeyPress(K::One, true) && d.calls == 20);
    assert(d.onChoiceKeyPress(K::Two, false) && d.calls == 20);
    env.manager.choices = {{"not yet displayed", 987}};
    assert(d.onChoiceKeyPress(K::One, false) && d.calls == 20);
    env.manager.choices = d.mChoices;
    env.manager.goodbye = true;
    assert(d.onChoiceKeyPress(K::One, false) && d.calls == 20);
    env.manager.goodbye = false;
    d.mGoodbye = true; assert(!d.onChoiceKeyPress(K::One, false)); d.mGoodbye = false;
    d.visible = false; assert(!d.onChoiceKeyPress(K::One, false)); d.visible = true;
    d.mPtr.empty = true; assert(!d.onChoiceKeyPress(K::One, false)); d.mPtr.empty = false;
    assert(d.onChoiceKeyPress(K::One, false) && d.answer == 567 && d.calls == 21);
    d.mChoices.clear(); assert(!d.onChoiceKeyPress(K::One, false));
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = pathlib.Path(directory)
    (path/'test.cpp').write_text(fixture + method + checks)
    subprocess.run(['g++', '-std=c++20', '-Wall', '-Wextra', '-Werror', str(path/'test.cpp'), '-o', str(path/'test')], check=True)
    subprocess.run([str(path/'test')], check=True)
print('PASS: both digit rows, nonsequential script IDs, tenth response, repeated keys, stale choices, bounds, goodbye, visibility and missing actor')
