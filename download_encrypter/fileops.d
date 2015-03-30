syscall:::entry
/( execname == "Chromium Helper") && probefunc != "kevent64" && probefunc != "kevent" && probefunc != "psynch_mutexwait" && probefunc != "psynch_mutexdrop"/ 
{
	printf("%s( %d )", probefunc, arg0);    
}

