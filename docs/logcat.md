# 

_android-vulkan_ project is using `android_vulkan::C++` tag for logging. But it's better to check output from the following standart libraries too:

- _libc_
- _gralloc_
- _ion_
- _Gralloc3_

So recommended _Logcat_'s regex filter is:

```txt
^(?:android_vulkan\:\:C\+\+|libc|gralloc|ion|Gralloc3)$
```
