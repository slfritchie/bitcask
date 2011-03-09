An unorthodox port of Bitcask
=============================

See the underlying fork at https://www.github.com/basho/bitcask for
the original source of Bitcask.

SLF's notes on the slf-rb-tree-hack2 branch
-------------------------------------------

First, confession time.  It's OK, you can laugh at this branch.
It's C.  Mostly.  It turns out that my C was rustier than I'd
thought.  So the savage surgery that I've done on this branch of
Bitcask, `slf-rb-tree-hack2`, is indeed pretty brute-force and
idealistic.  But hey, it now passes all of Bitcask's EUnit tests,
so it must work, right?

Second, why does this branch exist?  Because the original Bitcask
uses an in-memory hash table.  Hash tables are wonderfully fast, but
they do not preserve inter-key order properties.  And it would be
really, really great for some applications if Bitcask could preserve
inter-key ordering.

So, I forked the Bitcask repo and created the `slf-rb-tree-hack2`
branch to use a license-compatible (I hope) red-black binary tree
library for Bitcask's internal key management.  And it works, at
least a little bit.  Now it's time to let the world see it.  Really,
it's OK if you giggle.  You were given permission two paragraphs ago.
`:-)`

Status and Next Steps
---------------------

* All of the original Bitcask EUnit tests work for me: OS X 10.6 +
  64 bit and 32 bit, I think ... I've done some development work with
  a 32 bit version of the Erlang VM because the 64 bit version wasn't
  behaving nicely with Valgrind.  Or was it GDB?  Or both, I forget.
  But it's likely to work in 32 bit land.

* I have not done any testing with any other platform.  It is likely
  to work on all other platforms that Bitcask already supports, but
  I am not going to guarantee that yet.

* This code has not actually been stressed or even lightly tested with
  Riak.  That is a no-brainer next step, but I have a limited quota
  of keystrokes that I can use each day, so I haven't done it.  Yet.

* There's probably at least another round of refactoring to isolate the
  tree-specific data structure manipulation stuff into a tidier
  place.

* I added a new QuickCheck-based test, `test/rb_eqc.erl`, to test
  the red-black tree library.  After all, what if it was buggy?  It
  turns out that it mostly wasn't ... although QuickCheck was very
  happy to demonstrate to me that this particular library was more
  than happy to insert the same key again into the same tree instead
  of *replacing* that key.
    * Speaking of that library, it came with a 2-paragraph BSD-style
      "LICENSE" file.  Neither the LICENSE file nor any of the source
      files contain an author's name or email address or Web/FTP site
      or other helpful info like that.  So, I'll just add the tarball
      as-is to the repo to try to appease the provenance and licensing
      gods.
    * After Google-fueled searching, it seems likely that the author of the
      contents of the `c_src/rb_tree.tar.gz` file is Emin Martinian.
      See <http://www.mit.edu/~emin/source_code/red_black_tree/> for
      more info.

* There's plenty room for improvement with memory management and
  efficiency.  I have dim memories of putting the same number of
  keys into the original Bitcask and this tree-hacked Bitcask.  The
  tree-hacked version used about 35% more memory.  The goal is to use
  only as much RAM/key as the original Bitcask.

* One strategy for trying to reduce memory consumption is to use the
  `JudySL` library from the Judy library at
  <http://judy.sourceforge.net/>.  When combined with storing `BKey`s
  with the bucket name first, the common prefix of `bucket1/key1`,
  `bucket1/key2`, `bucket1/key3` might add up to some considerable
  memory savings?

Original README text
--------------------

Bitcask uses the "rebar" build system, but we have provided a wrapper
Makefile so that simply running "make" at the top level should work.

Bitcask requires Erlang R13B04 or later.

