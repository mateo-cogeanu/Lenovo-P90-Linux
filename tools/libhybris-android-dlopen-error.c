#include <dlfcn.h>
#include <hybris/common/binding.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    const char *name = argc > 1 ? argv[1] : "libsf_compat_layer.so";
    void *handle = android_dlopen(name, RTLD_NOW);
    const char *error = android_dlerror();
    printf("library=%s handle=%p error=%s\n", name, handle,
           error ? error : "(none)");
    return handle ? 0 : 1;
}
