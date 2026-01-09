\ -----------------------------------------
\ quine.f -- a program that prints its own
\ length and contents
\ basis.f is required
\ -----------------------------------------

$" quine.f" r/o file-open throw
            $" Length: " type 
            dup file-size throw . cr
            dup pad 800 rot file-read throw drop
            pad dup strlen buf-terminate type
            file-close throw
            quit
