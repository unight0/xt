\ -----------------------------------
\ list.f -- a test program for a few
\ Linked List functions
\ basis.f is required
\ -----------------------------------

0 constant nil

\ Linked List structure:
\ { Next (1 cell), Value (1 cell) }
( ll -- ll )
: ll->next @ ;
( ll -- *v )
: ll->*value 1 cells + ;
( ll -- v )
: ll->value ll->*value @ ;
( v -- tail )
: ll-new here nil , swap , ;
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
\ Get Linked List length
( head -- n )
: ll-length
  0
  while over nil != begin
    1+ swap ll->next swap
  done
  swap drop
;
\ Get the Nth element
\ If n > length(head), returns nil
( n head -- nth )
: ll-nth
  while
  over over nil != swap 0!= and
  begin
    ll->next swap 1- swap
  done swap drop
;
\ Print the linked list
( ll -- )
: ll.
  while dup nil != begin
    dup ll->value . 32 >term
    ll->next
  done
;

10 ll-new variable List

55 List @ ll-prepend to List
66 List @ ll-append drop
88 List @ ll-append drop
List @ ll. nl
