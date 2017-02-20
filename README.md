deviceconsole
============

This program shows system log entries of USB-connected iOS devices in the terminal in real-time, with syntax highlighting.  
Improved version includes extra filters, simulator support and support for colored HBLogMessages.

How To Build
----------
Download XCode (and its command line tools) and just enter `make` in the terminal to build

How To Use
----------

```````sh


Usage: deviceconsole [options]
Options:
-i | --case-insensitive     Make filters case-insensitive
-f | --filter <string>      Filter include by single word occurrences
-x | --exclude <string>     Filter exclude by single word occurrences
-p | --process <string>     Filter by process name
-u | --udid <udid>          Show only logs from a specific device
-s | --simulator <version>  Show logs from iOS Simulator
     --debug                Include connect/disconnect messages
     --use-separators       Skip a line between each line
     --force-color          Force colored text
Control-C to disconnect


````````
