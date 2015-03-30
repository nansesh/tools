syscall:::entry
/(execname == "Chromium") && ( probefunc == "open" ) && basename(copyinstr(arg0)) == "advanced-linux-programming (1).pdf" / 
{
    /*self->r = basename(copyinstr(arg0)) == "advanced-linux-programming (1).pdf" ? 1:0;
    ustack();
    printf("got it");*/
    ustack();
}

