# *Make and Escape*

![alt text](https://github.com/heyimglory/15-466-f17-base1/blob/master/screenshots/center.png)

[Design Document](http://graphics.cs.cmu.edu/courses/15-466-f17/game1-designs/hungyuc/)

## Asset Pipeline

The asset pipeline is pretty simple. It takes an altas and a txt file include the name of the texture and the coordinates. All the pipelie does is convert it into binary file and make sure the size if the information is correct. After the conversion, the main program can just take 20 characters as the name, and four float as the coordinate.

## Architecture

While running the game, it will determine which screen should display first. Then process the objects inside the screen. The objects have several status variable to determine whether they should show or interact with other objects. Most of them are divide into two types that share some traits when interacting with other objects.

## Reflection

It took me a long time to understand how the texture loading process works and figure out how should a asset pipeline be like. I'm not really satisfy with my asset pipeline this time because it seems not doing much. Things were not that difficut after loading the texture successully, but still need a lot of tome to get them done.

I implement my own design this time, so I think the design document is pretty clear. But I'm not sure if that's just because I already thought about those things before.


# About Base1

This game is based on Base1, starter code for game1 in the 15-466-f17 course. It was developed by Jim McCann, and is released into the public domain.

## Requirements

 - modern C++ compiler
 - glm
 - libSDL2
 - libpng

On Linux or OSX these requirements should be available from your package manager without too much hassle.

## Building

This code has been set up to be built with [FT jam](https://www.freetype.org/jam/).

### Getting Jam

For more information on Jam, see the [Jam Documentation](https://www.perforce.com/documentation/jam-documentation) page at Perforce, which includes both reference documentation and a getting started guide.

On unixish OSs, Jam is available from your package manager:
```
	brew install ftjam #on OSX
	apt get ftjam #on Debian-ish Linux
```

On Windows, you can get a binary [from sourceforge](https://sourceforge.net/projects/freetype/files/ftjam/2.5.2/ftjam-2.5.2-win32.zip/download),
and put it somewhere in your `%PATH%`.
(Possibly: also set the `JAM_TOOLSET` variable to `VISUALC`.)

### Bulding
Open a terminal (on windows, a Visual Studio Command Prompt), change to this directory, and type:
```
	jam
```

### Building (local libs)

Depending on your OSX, clone 
[kit-libs-linux](https://github.com/ixchow/kit-libs-linux),
[kit-libs-osx](https://github.com/ixchow/kit-libs-osx),
or [kit-libs-win](https://github.com/ixchow/kit-libs-win)
as a subdirectory of the current directory.

The Jamfile sets up library and header search paths such that local libraries will be preferred over system libraries.
