## Fan control for ThinkPad laptops

This is a small application to manually control the fan speeds for ThinkPad laptops running Linux. 
It's very much modeled on this project: [Stanko/ThinkPad-Fan-Control](https://github.com/Stanko/ThinkPad-Fan-Control), in particular [This fork](https://github.com/ForgedTurbo/ThinkPad-Fan-Control), which expanded on the original.

That repo is getting on a bit, however, and still relies on GTK2. I've essentially copied the UI roughly (that is to say: I eyeballed it), and added some small tweaks that I thought made sense, namely:

* Scan intervals being used to report the temperature in real time (including when in manual control).
* A dialog prompting to set the fan speed to AUTO when exiting the application.
* The ability to specify a scan interval for manual control that is different to the one for auto control.
* in terms of code: I removed a bunch of global variables, in favour of the `gpointer` argument passed to the signal handlers.

There are, of course a lot of things that can do with some improvements:

* Improve the Makefile
* Replace the `application` type to a composite passing only the parts that actually make sense to the various callbacks.
* Add the ability to set fan speeds for various temperature points (e.g. 30-50: speed 1, 51-60: speed 4, 61-70: speed 7, 71+; speed FULL).

### Compiling

All you really need is `gcc`, `pkg-config`, `make`, and have `gtk+-3` installed. If you don't have make, as you can see in the Makefile, the only thing that is needed to compile this is:

```bash
$ gcc -O2 -Wall -o build/fan_control src/main.c `pkg-config gtk+-3.0 --libs --cflags`
```

### More things to do

The UI, as mentioned, was slapped together quickly, and intended to look like the original project as much as possible. It's a bit janky ATM, though. The code is not too messy, but much like the UI, was written pretty much on the fly. It could be improved upon. As it stands, the project is working just fine on my T480, and I'm running it as I'm typing this readme. It does what it was intended to do, but I'm treating it as a hobby project for when I have some spare time for tinkering.

### Screenshots

On startup, you'll see the following screen, where you can specify a critical temperature, the fan speed to be applied when the CPU temp exceeds the critical temperature threshold, and a safe temperature at which point the fan gets reset to `AUTO` levels (ie the default profile). You can set a scan interval (in seconds) for how often you want the CPU temps to be checked.

![Auto screen](/data/screen1.png?raw=true "Auto control view")

The `Manual Control` tab allows you to forcefully set an immutable fan speed. There is a scan interval input here, too. This is used to specify how often you want to check the CPU temperature. The current CPU temp, fan speed, and time will be constantly updated as shown here:

![Manual screen](/data/screen2.png?raw=true "Manual control view")

When exiting the application, if the application has set the fan speed to anything other than `AUTO`, a dialog will appear asking whether or not you want to set the fan speed back to `AUTO` before exiting. Choosing _Cancel_ will close the dialog, and the application will stay open.

![Exit dialog](/data/screen3.png?raw=true "Exit dialog")
