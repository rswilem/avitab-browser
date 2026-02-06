## AviTab Browser

For X-Plane 11 and X-Plane 12.

[Download at x-plane.org](https://forums.x-plane.org/index.php?/files/file/93812-avitab-browser-a-browser-addon-for-the-avitab-plugin/)

The AviTab browser plugin is a browser that lets you browse the internet using your AviTab and will integrate with about every plane / tablet resolution, as long as there is an AviTab.json present in the airplane directory.
There's a special integration for the Zibo 738 as well.

A few features of the plugin:

- Configurable using .ini file
- Custom homepage
- A statusbar integration with up to five handy 'hotkey' websites
- Flightplan downloading from SimBrief directly to Output/FMS Plans
- Local browser cache (for login credentials, cookies and stuff)
- Geolocation support (your location comes from the sim, of course)

This plugin makes use of the already present CEF (Chromium Embedded Framework) in the X-Plane 12 binary. This plugin ships with CEF 117.2.5 for X-Plane 11. That's also why the X-Plane 11 download is relatively big in size. There's a lot of talk around using CEF in X-Plane. The problem with CEF is that there's at most only one instance allowed to be active. This is why for X-Plane 12 the plugin aims to use the integrated CEF version. This way X-Plane does not need to "uninitialize" their version, which gives all sorts of problems. For X-Plane 11 the plugin _does_ load a standalone version, so this could be problematic when opening the browser in X-Plane 11. See also https://developer.x-plane.com/2018/04/lets-talk-about-cef/. For X-Plane 12, this plugin simply creates a new "browser tab" in the already initialized CEF framework.

## Development Setup

### 1. XPlane SDK

Download the latest [X-Plane SDK](https://developer.x-plane.com/sdk/plugin-sdk-downloads/) to get started.
Place the file in the root folder as shown below and unzip it.

The latest SDK at the time of writing is X-Plane SDK 4.2.0.

```
avitab-browser/
│
├── README.md
├── LICENSE.md
├── .gitignore
│
├── SDK
|  ├── CHeaders
|  ├── Delphi
|  ├── Libraries
|  ├── ...
|  ├── README.txt
|
.....
```

### 2. CEF

a. Download CEF `117.2.5` (This may change as X-Plane 12 updates its CEF version, we try to stay on the same version)

- Windows: https://cef-builds.spotifycdn.com/index.html#windows64:117.2.5
- Linux: https://cef-builds.spotifycdn.com/index.html#linux64:117.2.5
- MacOS(arm): https://cef-builds.spotifycdn.com/index.html#macosarm64:117.2.5

Create a lib folder and put the downloaded file like this:

```
avitab-browser/
│
├── README.md
├── LICENSE.md
├── .gitignore
│
├── SDK
|
├── lib               #<----- lib
|  ├── mac_x64
|      ├── cef        #<------ unzip and rename downloaded cef
|         ├── cmake
|         ├── include
|         ├── ...
|         ├── README.txt
|  ├── lin_x64
|  ├── win_x64
.....
```

b. Build cef

`lib/mac_x64/cef/CMakeLists.txt` has instructions on how to build. you will need (on MacOS):

- `Chromium Embedded Framework.framework` under `cef` folder
- `libcef_dll_wrapper.a` copied from `cef/build/libcef_dll_wrapper/libcef_dll_wrapper.a` under `cef`

```
avitab-browser/
│
├── README.md
├── LICENSE.md
├── .gitignore
│
├── SDK
|
├── lib
|  ├── mac_x64
|      ├── cef
|         ├── cmake
|         ├── include
|         ├── ...
|         ├── README.txt
|         ├── README.txt
|         ├── Chromium Embedded Framework.framework    # <----------
|         ├── libcef_dll_wrapper.a.                    # <----------
|      ├── dist_301
|         ├── ... all embedded XP11 files. See the download for XP11/mac_x64 ...
|  ├── lin_x64
|      ├── To be determined...
|  ├── win_x64
|      ├── cef
|      ├── curl
|      ├── dist_301
|         ├── ... all embedded XP11 files. See the download for XP11/win_x64 ...
|      ├── dist_410
|         ├── ... all embedded XP11 files. See the download for XP12/win_x64 ...
.....
```

### 3. Compile X-Plane plugin

Use Xcode or build a release version using:
`./build_platforms.sh`

> NOTE: for development, you only need to setup dependencies of your OS so you can compile and test. If you are on MacOS, you may also want to change `toolchain-mac.cmake` to only include Intel or Arm build for development and quicker compilation. You could also use the xcodeproj file to build and run the plugin.
