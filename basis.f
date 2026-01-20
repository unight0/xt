: , here ! 1 cells allot ;
: b, here b! 1 allot ;
: [ 0 mode ! ; immediate compile-only
: ] -1 mode ! ; interpret-only
: 'lit [ ' lit throw dup , , ] ;
: lit, 'lit , , ;
: ['] ' throw lit, ; immediate compile-only
: postpone ' throw lit, ['] , , ; immediate compile-only

: if postpone 0branch here 0 , ; immediate compile-only
: else postpone branch here 1 cells + swap ! here 0 , ; immediate compile-only
: then here swap ! ; immediate compile-only

: while here ; immediate compile-only
: begin here 1 cells + postpone 0branch 0 , ; immediate compile-only
: done swap postpone branch , here swap ! ; immediate compile-only

: 1+ 1 + ;
: != = not ;
: 0= 0 = ;
: 0!= 0 != ;
: nl 10 >term ;

: \ while source> dup 10 != 0!= and begin done ; immediate
\ ('X' parse <cc>X -- str-ptr )
: parse
  *source swap
  while source> over over != begin
    0 = if refill throw then
  done
  drop drop
  0 *source 1 - b!
;
: ( ')' parse drop ; immediate
( this is a 
  multiline comment )

\ LIMIT INDEX do
\   <body>
\ loop
\ Note: do...loop is slow. Countdown from a million to zero while printing
\ 'hi\n' was 3.7 seconds with do...loop and only 3.2 seconds with
\ while...begin...done. This performance is on par with python.
\ Not bad, since this is an interpreted language with bytecode (so-to-say)
\ compilation. But do...loop needs to be rewritten.
( limit index R:ret-ptr -- R:index limit ret-ptr )
: (do) r> swap >r swap >r >r ; 
( R:index limit ret-ptr -- not-done? )
: (loop) r> r> r> over swap 1 + rot over < swap >r swap >r swap >r ;
: do postpone (do) here ; immediate compile-only
: loop
  postpone (loop) postpone 0branch ,
  \ Drop the limit and index off of the return stack
  postpone r> postpone drop postpone r> postpone drop
; immediate compile-only

: mem-total mem-end mem-begin - ;
: mem-used here mem-begin - ;
: mem-left mem-end here - ;

( s -- )
: type while dup b@ 0 != begin dup b@ >term 1 + done drop ;
( -- )
: words dict while dup 0 != begin dup ->name type 32 >term ->next done drop nl ;
( -- )
: count-words 0 dict while dup 0 != begin swap 1 + swap ->next done drop ;

\ Calculate the length of the string
( str -- len )
: strlen
  0 swap
  while dup b@ begin
    1 + swap 1 + swap
  done
  drop
;
\ Copy a string from src to dest
( src dest -- dest )
: strcpy
  dup rot swap
  while over b@ begin
    over over
    swap b@ swap b!
    1 + swap 1 + swap
  done
  drop 0 swap b!
;
\ Are strings equal?
( s1 s2 -- b )
: streq?
 while
   \ End of both strings
   over b@ over b@
   0!= swap 0!=
   and
 begin
    \ One string has ended before another?
    over over 0!= swap 0!= xor if drop drop 0 ret then
    \ Chars not equal?
    over b@ over b@ != if drop drop 0 ret then
    \ Advance
    1+ swap 1+
 done
 -1
;
\ Compile string push into a word
( s" <str>" -- str-ptr )
: s"  ['] strlit ,
  '"' parse here strcpy strlen 1 + allot
; immediate compile-only
\ A convenience function that mirrors s(
\ s( <str>) -- str-ptr
: s(  ['] strlit ,
  ')' parse here strcpy strlen 1 + allot
; immediate compile-only
: mem-report mem-used . '/' >term mem-total . s"  bytes used" type nl ; interpret-only
\ Works similar to s", but for interpretation mode
( $" <str>" -- str-ptr )
: $" '"' parse here strcpy dup strlen 1 + allot ; interpret-only
\ A convenience function that mirrors $"
\ $( <str>) -- str-ptr
: $( ')' parse here strcpy dup strlen 1 + allot ; interpret-only
\ These two are convenience functions
\ .( exists so that one can put " inside a string
\ to be printed
\ .( <str>) --
: .( ')' parse type ; immediate
\ (." <str>" -- )
: ." '"' parse type ; immediate

\ Modes of file access
: r/o 0 ;
: w/o 1 ;
: r/w 2 ;

\ Terminates buffer with a \0 character
\ Equivalent to buf[len] = '\0';
( buf len -- buf)
: buf-terminate over swap + 0 swap b! ;

\ Write a string into a file
( str fd -- num ior )
: file-writestr swap dup strlen rot file-write ;
\ Write a character into a file
( ch fd -- ior )
: file-putchar swap here b! here 1 rot file-write swap drop ;
\ Write a string to file, then append newline to file
( str fd -- )
: file-writeln 
    dup rot swap
    file-writestr dup 0 < if ret then
    drop 10 swap
    file-putchar
;

\ Creates a constant, which is a word that pushes
\ the specified value to stack upon being executed
( value constant <spaces>name -- )
: constant create , does> @ ;
\ Creates a variable, which is a word that pushes
\ the address of one cell to stack upon being executed
\ That cell is initialized with the specified value
( value variable <spaces>name -- )
: variable create , ;
\ Write value to variable X
\ This is a state-smart word
( value to <spaces>X -- )
: to mode @ not if
    \ Interpretation
    ' throw execute ! ret then
    \ Compilation
    ' throw lit, postpone execute postpone !
; immediate

\ About true and false:
\ It is important that true = -1
\ This is the same as 0XFF...FF
\ The bitwise operations or, and, xor in our case also
\ double as 'logical' or, and, and xor
0 constant false
-1 constant true

0 constant nil

\ Linked List structure:
\ { Next (1 cell), Value (1 cell) }
( ll -- ll )
: ll->next @ ;
( ll -- v )
: ll->value 1 cells + @ ;
( v -- tail )
: ll-new  here nil , swap , ;
( v head -- head )
: ll-prepend here swap , swap , ;
( v after -- new-node )
: ll-insert
    dup ll->next here swap ,
    rot ,
    over !
;
\ Find the tail node of the linked list
( ll -- tail )
: ll-tail
  while dup ll->next nil != begin
    ll->next
  done
;
\ Append a new element after the tail node
( v ll -- new-node )
: ll-append
    ll-tail ll-insert
;
\ Print the linked list
( ll -- )
: ll.
  while dup nil != begin
    dup ll->value . 32 >term
    ll->next
  done
;

\ Require functionality
nil variable Sources
( file -- )
: included
    Sources @ ll-prepend Sources !
;
( file -- )
: included?
  Sources @
  while dup nil != begin
    over over
    ll->value
    streq? if drop drop true ret then
    ll->next
  done
  drop drop false
;
\ Evaluate the contents of the specified file
\ Example usage: $" test.f" include
( filename -- <???> )
: include
  dup included? not if dup included then
  r/o file-open throw file-as-source throw
;
\ Same as include, but checks if the file was included before
\ to avoid double-inclusion
( filename -- <???> )
: require
  dup included? if drop ret then
  include
;

\ EXPERIMENTAL
\ A convenience word for allocating some memory on the
\ heap and installing it as the current working memory
( sz -- ior )
: mem-new
    dup heap-allocate
    dup if ret then drop
    swap mem-install
;
