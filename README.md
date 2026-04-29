## TINT2 is still not dead

I had to adopt tint2 from [nick87720z](https://github.com/nick87720z/tint2) who adopted it from [o9000](https://gitlab.com/o9000/tint2) account rather automatically after it was suddenly sentenced to end-of-life by previous maintainer without warning.

This extremely simple panel was my daily driver for years, so it is time for me to start giving back.

This repo fixes several issues already, most notably snprintf-related crashes and crashes when the last item is removed from tray.

I test on latest Arch, but I am willing to try and replicate issues on various platforms my box is able to support.

Any help is welcome.

*Code does not die. Not as long as there's compiler, able to build it, and system, able to run it, in the world. Moreover while it's FLOSS project.*

- Nikita Zlobin

# How do I get it?

AUR: https://aur.archlinux.org/packages/tint2-patched-git

Changes: [changelog.md](changelog.md)

Documentation: [doc/tint2.md](doc/tint2.md)

Compile it with (after you install the [dependencies](https://gitlab.com/o9000/tint2/wikis/Install#dependencies)):

```bash
mkdir -p tint2/build
cd tint2
git clone https://github.com/korbeljak/tint2.git
cd build
cmake ../tint2
make -j4
```

To install, run (as root):

```bash
make install
update-icon-caches /usr/local/share/icons/hicolor
update-mime-database /usr/local/share/mime
```

or on Arch:

```bash
gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor
```


And then you can run the panel `tint2` and the configuration program `tint2conf`.

Please report any problems to [Issues](https://github.com/korbeljak/tint2/issues). Your feedback is much appreciated.


# What is tint2?

tint2 is a simple panel/taskbar made for modern X window managers. It was specifically made for Openbox but it should also work with other window managers (GNOME, KDE, XFCE etc.). It is based on ttm https://code.google.com/p/ttm/.

# Features

  * Panel with configurable set of applets:
    - taskbar, system tray, clock, launcher
    - arbitrary buttons with configurable commands per each mouse button (including scroll buttons)
    - executors - like buttons, but using configurable command to set appearance (see manual)
  * Easy to customize:
    - Color/transparency on fonts, icons, borders and backgrounds;  
    **Note:** Full transparency requires a compositor such as Compton (if not provided already by the window manager, as in Compiz/Unity, KDE or XFCE);
    - Customizable mouse events;
    - Customizable interpreters for action commands;
  * Pager like capability: move tasks between workspaces (virtual desktops), switch between workspaces;
  * Multi-monitor capability: create a panel for each monitor, showing only the tasks from that monitor;

# Goals

  * Be unintrusive and light (in terms of memory, CPU and aesthetic);
  * Follow the freedesktop.org specifications;
  * Make certain workflows, such as multi-desktop and multi-monitor, easy to use.

# I want it!

  * [Install tint2](https://gitlab.com/o9000/tint2/wikis/Install)

# How do I ...

  * [Install](https://gitlab.com/o9000/tint2/wikis/Install)
  * [Configure](doc/tint2.md)
  * [Add applet not supported by tint2](https://gitlab.com/o9000/tint2/wikis/ThirdPartyApplets)
  * [Other frequently asked questions](https://gitlab.com/o9000/tint2/wikis/FAQ)
  * [Obtain a stack trace when tint2 crashes](https://gitlab.com/o9000/tint2/wikis/Debug)

# Known issues

  * [Add one!](https://github.com/korbeljak/tint2/issues)

## Development issues

  * I still need to learn more about the codebase

# How can I help out?

  * Just fork this and create pull requests, even empty, then we talk.
  * You can report issues and request features, chances are somebody picks them up.

# Links
  
  * Documentation: https://gitlab.com/o9000/tint2/wikis/home
  * Old project locations (inactive):
    * https://gitlab.com/nick87720z/tint2
    * https://gitlab.com/o9000/tint2
    * https://code.google.com/p/tint2

# Screenshots

Post some and let me know!

## Demos

* [Compact panel, separator, color gradients](https://gitlab.com/o9000/tint2/wikis/whats-new-0.13.0.gif)
* [Executor](https://gitlab.com/o9000/tint2/wikis/whats-new-0.12.4.gif)
* [Mouse over effects](https://gitlab.com/o9000/tint2/wikis/whats-new-0.12.3.gif)
* [Distribute size between taskbars, freespace](https://gitlab.com/o9000/tint2/wikis/whats-new-0.12.gif)