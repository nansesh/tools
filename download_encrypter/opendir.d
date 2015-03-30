syscall:::entry
/( execname == "Chromium Helper" || execname == "Chromium" ) && probefunc == "opendir"/
{
        printf("%s( %s )", probefunc, copyinstr(arg0));
}
