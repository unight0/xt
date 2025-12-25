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

: \ while t>b dup 10 != and begin done ; immediate

\ ('X' parse <cc>X -- str-ptr )
: parse
  &t swap
  while t>b over over != begin
      not if refill throw then
  done
  drop drop
  0 &t 1 - b!
;
: ( ')' parse drop ; immediate
( this is a 
  multiline comment )

\ LIMIT INDEX do
\   <body>
\ loop
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

: mem-used here mem-begin - ;
: mem-left mem-end here - ;

( s -- )
: type while dup b@ 0 != begin dup b@ b>t 1 + done drop ;
( -- )
: words dict while dup 0 != begin dup ->name type 32 b>t ->next done 10 b>t ;

( str -- len )
: strlen
  0 swap
  while dup b@ begin
    1 + swap 1 + swap
  done
  drop
;
( dest src -- dest )
: strcpy
  \ Keep the inital dest pointer
  swap dup rot
  while dup b@ begin
    over over
    b@ swap b!
    1 + swap 1 + swap
  done
  drop 0 swap b!
;
( s" <str>" -- str-ptr )
: s" 
  ['] strlit ,
  while t>b dup dup '"' != and begin b, done
  0 b,
  drop
; immediate compile-only
: .( ')' parse type ; immediate

\ words

: t<< 10 parse type cr ;
\ t<< One two three four?
: test 1000 0 do 1 loop ;
\ test
