timing/tri/
===========

Time results needed to make the text of the book are under git control in
this repo.  Thus they do not need to be re-generated to build the book.
The book can be built without a working PETSc install.

To redo the timing results do

    $ (cd ../../ && make tri)
    $ ./time.sh
