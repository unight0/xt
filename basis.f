: , here ! 1 cells allot ;
: b, here b! 1 allot ;
: [ 0 mode b! ; immediate compile-only
: ] 1 mode b! ; interpret-only
: 'lit [ ' lit dup , , ] ;
: lit, 'lit , , ;
: ['] ' lit, ; immediate compile-only
: postpone ' lit, ['] , , ; immediate compile-only

: if postpone 0branch here 0 , ; immediate compile-only
: else postpone branch here 1 cells + swap ! here 0 , ; immediate compile-only
: then here swap ! ; immediate compile-only

: while here ; immediate compile-only
: begin here 1 cells + postpone 0branch 0 , ; immediate compile-only
: done swap postpone branch , here swap ! ; immediate compile-only

: != = not ;

: \ while src>b dup 10 != and begin done ; immediate
\ ('X' parse <cc>X -- str-ptr )
: parse
  &src swap
  while src>b over over != begin
    not if refill throw then
  done
  drop drop
  0 &src 1 - b!
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
: type while dup b@ 0 != begin dup b@ b>t 1 + done drop ;
( -- )
: words dict while dup 0 != begin dup ->name type 32 b>t ->next done drop cr ;
( -- )
: count-words 0 dict while dup 0 != begin swap 1 + swap ->next done drop ;

( str -- len )
: strlen
  0 swap
  while dup b@ begin
    1 + swap 1 + swap
  done
  drop
;
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
\ Compile string push into a word
( s" <str>" -- str-ptr )
: s"  ['] strlit ,
  '"' parse here strcpy strlen 1 + allot
; immediate compile-only
: mem-report mem-used . '/' b>t mem-total . s"  bytes used" type cr ;
\ Works similar to s", but for interpretation mode
( $" <str>" -- str-ptr)
: $" '"' parse here strcpy dup strlen 1 + allot ; interpret-only
\ .( <str>) --
: .( ')' parse type ; immediate

\ Modes of file access
: r/o 0 ;
: w/o 1 ;
: r/w 2 ;

\ Evaluate the contents of the specified file
\ Example usage: $" test.f" include
( filename -- )
: include r/o file-open throw file-as-source throw ;

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

\ $" test" r/w file-open throw
\          dup $" Hello!" swap file-writeln
\          file-close quit
