# Elf_Loader 学习

[参考文档](https://github.com/WonderfulCat/WonderfulCat.github.io/blob/master/archive/Android4.1_ELF_Loader.md)
```c
root@android:/data/user # ./hello
fp - 0xb6f4bc9c  
head - ELF   
mmap = 0x80000000
mmap = 0x80018000
mmap = 0x8001b000
mmap ok
dyn_entry : 0x80018be8
         DT_STRTAB : 0x80000cc8  
         --------------------    
         DT_NEEDED : liblog.so   
         DT_NEEDED : libcutils.so
         DT_NEEDED : libc.so      
         DT_NEEDED : libusbhost.so
         DT_NEEDED : libstdc++.so 
         DT_NEEDED : libm.so      
         --------------------     
         DT_SYMTAB : 0x80000148
         --------------------  
         DT_JMPREL : 0x80003394
         DT_PLTRELSZ : 162     
         --------------------  
         DT_REL : 0x800018fc   
         DT_RELSZ : 851        
         --------------------
         nbucket : 83
         nchain : b8
         bucket : 80001410
         chain : 8000161c
         __dso_handle, ELF32_ST_BIND = 1,st_shndx = 21, addr = 1a420  
         __FINI_ARRAY__, ELF32_ST_BIND = 1,st_shndx = 15, addr = 18a08
         __INIT_ARRAY__, ELF32_ST_BIND = 1,st_shndx = 14, addr = 18a00
         __bss_start, ELF32_ST_BIND = 1,st_shndx = 65521, addr = 1a418
         _edata, ELF32_ST_BIND = 1,st_shndx = 65521, addr = 1a418     
         _end, ELF32_ST_BIND = 1,st_shndx = 65521, addr = 1f780       
======================================
lrwxrwxrwx root     root              1970-01-01 00:00 0 -> /data/data/
-rwxrwxrwx root     root       589588 2017-09-14 07:08 android_server  
-rwxrwxrwx root     root        17528 2024-07-12 13:49 hello
-rwxrwxrwx root     root       105028 2024-07-10 19:54 ls
```
