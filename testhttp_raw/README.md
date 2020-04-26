# testhttp_raw.c
Simple program for my Computer Network classes
---
### COMPLE AND RUN:

To compile the code, use ``` make ``` in the source folder.

Run ``` ./testhttp_raw <connection_address> <cookies_file< <test_address>```

Program 

Establishes connection with the server given in ``` connection_address ```

Creates a GET request for ``` test_address ``` with cookies fetched from  ``` cookies_file ``` 

``` cookies_file ``` contains cookies to be sent by the program in get request, each cookie in a new line in the following 
format:

``` <cookie_name>=<cookie_value> ``` where ``` cookie_name ```  and ``` cookie_value ``` are up to https://tools.ietf.org/html/rfc7230 directives

### RESULT

If the answer status is ``` 200 OK ```, program writes cookies fetched from the server response on standard output. 
Then it writes a line ``` Dlugosc zasobu: x ```, where ``` x ``` is the real length of the message-body. 

If the status answer is not ``` 200 OK ```, program writes the status line on the standard output and exits.

Any errors in memory allocation and invalid arguments are reported with messages on stderr, followed by exiting the program.
