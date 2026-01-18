\ -----------------------------------------
\ quine.f -- a program that prints its own
\ length and contents
\ basis.f is required
\ -----------------------------------------

$" quine.f" r/o file-open throw
            ." Length: " dup file-size throw . nl
            dup pad 800 rot file-read throw 
            pad swap buf-terminate type
            file-close throw
            quit
