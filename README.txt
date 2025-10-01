## CSOPESY: Marquee Console
### by S13 Group 6

## Members
- Almin, Wesner III C.
- Artus, Arianne Yari A.
- Perol, Francine Nicole T.
- Rueco, Gabrielle Gille T.

## Running the Code

### Option 1 - Visual Studio
- Open the project on Visual Studio
- Build the project
- Run using Visual Studio

### Option 2 - Windows Command Prompt
- Open the command prompt
- Compile and run with the following commands:
```bash
g++ main.cpp utils.cpp keyboard.cpp interpreter.cpp display.cpp marquee.cpp globals.cpp -o main.exe -pthread
```
```bash
main.exe
```

### Option 3 - VS Developer Command Prompt
- Open "x64 Native Tools Command Prompt for VS <version>"
- Change your directory to the project folder
- Compile and run with the following commands:
```bat
cl /std:c++17 /EHsc main.cpp utils.cpp keyboard.cpp interpreter.cpp display.cpp marquee.cpp globals.cpp
```
```bat
main.exe
```

## Entry Class File
- Main function is located inside `main.cpp`
