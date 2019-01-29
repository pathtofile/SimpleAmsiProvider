# Simple AMSI Provider Logger
This simple AMSI provider will attempt to log all strings passed into it to 'scan'.
This can be used to see what systems do and don't pass into AMSI.


## Requirements
Project requires at least `Windows 8.1`, testing only done on `Windows 10`

## Instructions

### Building
1. Load the Visual Studio solution.
2. Build the Project.

### Installation
1. From an elevated command prompt, go to the output directory and type
````
regsvr32 AmsiProvider.dll
````

### Uninstalling
1. From an elevated command prompt, go to the output directory and type
````
regsvr32 /u AmsiProvider.dll
````

## Reading Logs
This provider will produce ETW events to the log `00604c86-2d25-46d6-b814-cd149bfdf0b3`.
These events will contain any ASCII-printable data passed into the provider to scan
There are a number of ways to read these logs:

### ETWTracer
The simplest way is to use my other project `ETWTracer`. Once built run the following from
an elevated command prompt:
```
etwTracer.exe "00604c86-2d25-46d6-b814-cd149bfdf0b3"
```
Any data passed to the provider will be output the screen.

### Generating .etl files
Events logged by the sample provider can be captured using ETW tools such as xperf. The log files are generated in
ETL format so they can be viewed and processed by the Windows Performance
Toolkit (WPT), as well as utilities such as tracerpt.exe or xperf.exe.

1. From an elevated command prompt, type
````
xperf.exe -start mySession -f myFile.etl -on 00604c86-2d25-46d6-b814-cd149bfdf0b3
````
to begin capturing events from the provider used by the sample.

Once finished, run the following:
````
xperf.exe -stop mySession
````
to stop capturing events. Then, view the `myFile.etl` trace graphically in WPA, or generate a text version by typing `tracerpt myFile.etl`.

## Testing
To test, simply install the provider using `regsvr32`, start the ETW logging,
then just load up `powershell` and type some commands.

## Troubleshooting
Sometimes Windows Defender or other products that are ahead of us in the 'queue' will mark commands a `known good`, which will then
stop our provider from recieving scripts. To deal with this:
1. delete/rename any key under `HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\AMSI\Providers` that isn't `00604c86-2d25-46d6-b814-cd149bfdf0b3`.
   1.  **WARNING** this means you're essentially disabling any real AMSI scanning, so only do on a test machine!
2. Find a way to put out provider ahead of any other. TBD how to do, but should be possible pretty easily.
