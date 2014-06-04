!CHAPTER JavaScript Modules

!SUBSECTION Introduction to Javascript Modules

The ArangoDB uses a [CommonJS](http://wiki.commonjs.org/wiki)
compatible module and package concept. You can use the function `require` in
order to load a module or package. It returns the exported variables and
functions of the module or package.

There are some extensions to the CommonJS concept to allow ArangoDB to load
Node.js modules as well.
