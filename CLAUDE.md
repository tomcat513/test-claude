# CLAUDE.md

This file provides guidance for AI assistants working with this repository.

## Project Overview

**test-claude** is a test/sandbox repository used for experimenting with Claude Code. It contains a SwiftUI iPhone app called **SpinTriangle**.

## Repository Structure

```
.
├── CLAUDE.md                        # AI assistant guidance (this file)
├── LICENSE                          # Apache License 2.0
├── README.md                        # Project description
└── SpinTriangle/                    # iOS app (Xcode project)
    ├── SpinTriangle.xcodeproj/      # Xcode project configuration
    │   └── project.pbxproj
    └── SpinTriangle/                # App source code
        ├── SpinTriangleApp.swift    # App entry point (@main)
        ├── ContentView.swift        # Main UI with spinning triangle
        └── Assets.xcassets/         # Asset catalog (app icon, colors)
```

## Tech Stack

- **Language:** Swift 5
- **UI Framework:** SwiftUI
- **Minimum iOS:** 16.0
- **Xcode:** 15.0+
- **No external dependencies**

## Building & Running

1. Open `SpinTriangle/SpinTriangle.xcodeproj` in Xcode
2. Select an iPhone simulator or connect a physical device
3. Press Cmd+R to build and run
4. For physical devices: Xcode will prompt to set up a signing team (free Apple ID works)

## App Description

SpinTriangle is a minimal proof-of-concept app that displays a gradient-filled triangle. Tapping the "Spin" button rotates the triangle 360 degrees with an easeInOut animation. Each tap triggers another full rotation.

## License

Apache License 2.0

## Git Conventions

- Default remote branch: `main`
- Commit messages should be clear and descriptive
- Feature branches should use the `claude/` prefix when created by AI assistants

## Guidelines for AI Assistants

- Keep changes focused on what is explicitly requested
- Follow standard Swift/SwiftUI conventions when modifying the app
- Update this CLAUDE.md file when significant project infrastructure changes
- The bundle identifier `com.example.SpinTriangle` should be changed if publishing to the App Store
