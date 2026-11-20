"""Execute the production loading-screen progress/draw bodies with counted UI/viewer doubles.

The doubles cover admission, progress presentation and graphics-service calls;
they do not emulate a graphics driver or measure game FPS.
"""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def body(source, signature):
    start = source.index('{', source.index(signature))
    end, depth = start + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'apps/openmw/mwgui/loadingscreen.cpp')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    source = args.source.read_text()
    signatures = [
        'void LoadingScreen::setProgressRange(size_t range)',
        'void LoadingScreen::setProgress(size_t value)',
        'void LoadingScreen::increaseProgress(size_t increase)',
        'bool LoadingScreen::needToDrawLoadingScreen()',
        'void LoadingScreen::refresh(bool force)',
        'void LoadingScreen::draw(bool force)',
    ]
    production = '\n'.join(signature + '\n' + body(source, signature) for signature in signatures)
    preamble = r'''
#include <algorithm>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <vector>
using std::size_t;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct Bar {
    size_t range = 101;
    int width = 100, line = 10000, track = -1, trackWrites = 0, positionWrites = 0;
    void setScrollRange(size_t value) { range = value; }
    size_t getScrollRange() const { return range; }
    int getWidth() const { return width; }
    int getLineSize() const { return line; }
    void setScrollPosition(int value) { require(value == 0, "scroll position"); ++positionWrites; }
    void setTrackSize(int value) { track = value; ++trackWrites; }
};
namespace osg {
    struct Stats { int loading = 0; void setAttribute(unsigned, const char*, int value) { loading = value; } };
    struct FrameStamp {
        unsigned number = 0;
        unsigned getFrameNumber() const { return number; }
        double getSimulationTime() const { return 0; }
    };
}
namespace osgUtil {
    struct IncrementalCompileOperation {
        double minimum = 0; unsigned maximum = 0;
        void setMinimumTimeAvailableForGLCompileAndDeletePerFrame(double value) { minimum = value; }
        void setMaximumNumOfObjectsToCompilePerFrame(unsigned value) { maximum = value; }
    };
}
struct Viewer {
    Bar* bar;
    osg::Stats stats;
    osg::FrameStamp stamp;
    osgUtil::IncrementalCompileOperation compile;
    int events = 0, updates = 0, graphics = 0, advances = 0;
    std::vector<int> presentedTracks;
    osg::Stats* getViewerStats() { return &stats; }
    osg::FrameStamp* getFrameStamp() { return &stamp; }
    osgUtil::IncrementalCompileOperation* getIncrementalCompileOperation() { return &compile; }
    void eventTraversal() { ++events; }
    void updateTraversal() { ++updates; }
    void renderingTraversals() { ++graphics; presentedTracks.push_back(bar->track); }
    void advance(double) { ++advances; ++stamp.number; }
};
struct Resources { int reports = 0; void reportStats(unsigned, osg::Stats*) { ++reports; } };
namespace MWBase {
    struct Input {
        int calls = 0;
        void update(int duration, bool first, bool second) {
            require(duration == 0 && first && second, "input service arguments"); ++calls;
        }
    };
    struct Environment {
        Input input;
        static Environment& get() { static Environment value; return value; }
        Input* getInputManager() { return &input; }
    };
}
struct Timer { double now = 1; double time_m() const { return now; } };
struct LoadingScreen {
    Bar bar;
    Viewer viewer{&bar};
    Resources resources;
    Bar* mProgressBar = &bar;
    Viewer* mViewer = &viewer;
    Resources* mResourceSystem = &resources;
    Timer mTimer;
    double mLastRenderTime = 0, mLoadingOnTime = 1, mLastWallpaperChangeTime = 0;
    size_t mProgress = 0;
    bool mShowWallpaper = false;
    int mNestedLoadingCount = 1, copies = 0, wallpapers = 0;
    double getTargetFrameRate() const { return 120; }
    void changeWallpaper() { ++wallpapers; }
    void setupCopyFramebufferToTextureCallback() { ++copies; }
    void setProgressRange(size_t range);
    void setProgress(size_t value);
    void increaseProgress(size_t increase = 1);
    bool needToDrawLoadingScreen();
    void refresh(bool force = false);
    void draw(bool force = false);
};
'''
    tests = r'''
int main() {
    LoadingScreen screen;
    screen.setProgressRange(5000);
    for (int i = 0; i < 2000; ++i) screen.increaseProgress();
    require(screen.mProgress == 2000, "lost per-object progress");
    require(screen.viewer.graphics == 0 && screen.bar.trackWrites == 0
        && screen.bar.positionWrites == 0, "hidden progress changed widgets or rendered");
    screen.mTimer.now = 2;
    screen.refresh(true);
    const auto expected = static_cast<int>(2000 / 5001.f * 10000);
    require(screen.viewer.presentedTracks.back() == expected, "forced refresh presented stale progress");
    require(screen.copies == 1 && screen.bar.trackWrites == 1, "first forced draw or snapshot");
    for (int i = 0; i < 10000; ++i) screen.increaseProgress();
    require(screen.mProgress == 5000 && screen.viewer.graphics == 1 && screen.bar.trackWrites == 1,
        "unadmitted increments mutated UI, exceeded range or changed cadence");
    screen.mTimer.now = 200;
    screen.increaseProgress(0);
    require(screen.viewer.graphics == 2 && screen.bar.trackWrites == 2, "due frame did not run");
    screen.refresh(true);
    require(screen.viewer.graphics == 3, "stationary bar suppressed forced graphics work");
    require(screen.viewer.events == 3 && screen.viewer.updates == 3 && screen.viewer.advances == 3
        && screen.resources.reports == 3 && MWBase::Environment::get().input.calls == 3,
        "admitted draw omitted input, update, graphics, stats or frame advance");
    require(screen.viewer.compile.minimum == 1.0 / 120 && screen.viewer.compile.maximum == 1000,
        "loading graphics compilation budget changed");
    screen.setProgressRange(100);
    screen.refresh(true);
    require(screen.viewer.presentedTracks.back() == 0 && screen.mProgress == 0, "range reset was not presented");
    screen.setProgress(80);
    screen.refresh(true);
    require(screen.viewer.presentedTracks.back() == static_cast<int>(80 / 101.f * 10000), "absolute progress");
    screen.setProgress(20);
    screen.refresh(true);
    require(screen.viewer.presentedTracks.back() == static_cast<int>(20 / 101.f * 10000), "decreasing progress");
    const int beforeInactive = screen.viewer.graphics;
    screen.mNestedLoadingCount = 0;
    screen.refresh(true);
    require(screen.viewer.graphics == beforeInactive, "inactive refresh drew");
    screen.mNestedLoadingCount = 2;
    screen.refresh(true);
    require(screen.viewer.graphics == beforeInactive + 1, "nested active refresh omitted");
    screen.setProgressRange(0);
    screen.increaseProgress(20);
    screen.refresh(true);
    require(screen.mProgress == 0 && screen.viewer.presentedTracks.back() == 0, "empty range");
    LoadingScreen small;
    small.setProgressRange(10000);
    small.bar.width = 0;
    small.setProgress(1000);
    require(small.mProgress == 0 && small.viewer.graphics == 0, "zero-width admission changed");
    small.bar.width = 100;
    small.setProgress(1);
    require(small.mProgress == 0 && small.viewer.graphics == 0, "subpixel absolute admission changed");
    for (int i = 0; i < 50; ++i) small.increaseProgress();
    small.refresh(true);
    require(small.mProgress == 50 && small.bar.trackWrites == 1, "subpixel increments were lost or repeated UI writes");
    small.setProgress(1000);
    small.mTimer.now = 8;
    small.increaseProgress();
    require(small.viewer.graphics == 1, "frame-rate gate changed");
    small.mTimer.now = 200;
    small.increaseProgress();
    require(small.viewer.graphics == 2 && small.bar.trackWrites == 2, "visible progress did not catch up");
    std::cout << "PASS: 12000 object increments -> two admitted progress-widget writes; exact counters, forced/reset/decreasing/empty progress, zero-width/subpixel admission, nested refresh and unchanged graphics service/budget\n";
}
'''
    with tempfile.TemporaryDirectory(prefix='openmw-loading-progress-') as directory:
        directory = Path(directory)
        cpp = directory / 'test.cpp'
        cpp.write_text(preamble + production + tests)
        if shutil.which('cl'):
            binary = directory / 'test.exe'
            command = ['cl', '/nologo', '/std:c++20', '/EHsc', '/O2', '/W4', '/WX',
                       '/wd4244', str(cpp), '/Fe:' + str(binary)]
        else:
            binary = directory / 'test'
            command = ['g++', '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
                       '-Wno-missing-field-initializers', str(cpp), '-o', str(binary)]
        subprocess.run(command, check=True, cwd=directory, timeout=120)
        result = subprocess.run([str(binary)], check=True, capture_output=True, text=True, timeout=30)
        print(result.stdout, end='')
    if args.output:
        args.output.write_text(json.dumps({
            'result': 'PASS', 'source_sha256': hashlib.sha256(args.source.read_bytes()).hexdigest(),
            'production_methods': signatures, 'compiler': command[0], 'stdout': result.stdout,
            'limitations': 'Counted UI/viewer doubles; not a real GPU, full MyGUI runtime or gameplay benchmark.',
        }, indent=2) + '\n')


if __name__ == '__main__':
    main()
