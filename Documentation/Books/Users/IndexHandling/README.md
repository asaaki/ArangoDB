!CHAPTER Handling Indexes

!SUBSECTION Indexes, Identifiers, Handles 

This is an introduction to ArangoDB's interface for indexes in general.  
There are special sections for 

- [cap constraints](../IndexCap/README.md) 
- [geo-spatial indexes](../IndexGeo/README.md)
- [hash indexes](../IndexHash/README.md)
- [skip-lists](../IndexSkiplist/README.md)

!SUBSUBSECTION Index

Indexes are used to allow fast access to documents. For each collection there is always the primary index which is a hash index for the document key (_key attribute). This index cannot be dropped or changed.
Edge collections will also have an automatically created edges index, which cannot be modified. This index provides quick access to documents via the _from and _to attributes.

Most user-land indexes can be created by defining the names of the attributes which should be indexed. Some index types allow indexing just one attribute (e.g. fulltext index) whereas other index types allow indexing multiple attributes.

Indexing system attributes such as _id, _key, _from, and _to in user-defined indexes is not supported by any index type. Manually creating an index that relies on any of these attributes is unsupported.

!SUBSUBSECTION Index Handle
An index handle uniquely identifies an index in the database. It is a string and consists of a collection name and an index identifier separated by /.

!SUBSUBSECTION Geo Index

A geo index is used to find places on the surface of the earth fast.

!SUBSUBSECTION Hash Index

A hash index is used to find documents based on examples. A hash index can be created for one or multiple document attributes.
A hash index will only be used by queries if all indexed attributes are present in the example or search query, and if all attributes are compared using the equality (== operator). That means the hash index does not support range queries.

If the index is declared unique, then access to the indexed attributes should be fast. The performance degrades if the indexed attribute(s) contain(s) only very few distinct values.

!SUBSUBSECTION Edges Index

An edges index is automatically created for edge collections. It contains connections between vertex documents and is invoked when the connecting edges of a vertex are queried. There is no way to explicitly create or delete edge indexes.

!SUBSUBSECTION Skiplist Index

A skiplist is used to find ranges of documents.

!SUBSUBSECTION Fulltext Index

A fulltext index can be used to find words, or prefixes of words inside documents. A fulltext index can be set on one attribute only, and will index all words contained in documents that have a textual value in this attribute. Only words with a (specifyable) minimum length are indexed. Word tokenisation is done using the word boundary analysis provided by libicu, which is taking into account the selected language provided at server start. Words are indexed in their lower-cased form. The index supports complete match queries (full words) and prefix queries.

!SECTION Address and ETag of an Index

All indexes in ArangoDB have an index handle. This handle uniquely defines an
index and is managed by ArangoDB. The interface allows you to access the indexes
of a collection as:

    db.collection.index(index-handle)

For example: Assume that the index handle, which is stored in the `_id`
attribute of the index, is `demo/362549736` and the index lives in a collection
named *demo*, then that index can be accessed as:

    db.demo.index("demo/362549736")

Because the index handle is unique within the database, you can leave out the
*collection* and use the shortcut:

    db._index("demo/362549736")

!SECTION Which Index type to use when

ArangoDB automatically indexes the `_key` attribute in each collection. There
is no need to index this attribute separately. Please note that a document's
`_id` attribute is derived from the `_key` attribute, and is thus implicitly
indexed, too.

ArangoDB will also automatically create an index on `_from` and `_to` in any
edge collection, meaning incoming and outgoing connections can be determined
efficiently.

Users can define additional indexes on one or multiple document attributes.
Several different index types are provided by ArangoDB. These indexes have
different usage scenarios:

- hash index: provides quick access to individual documents if (and only if)
  all indexed attributes are provided in the search query. The index will only
  be used for equality comparisons. It does not support range queries.

  The hash index is a good candidate if all or most queries on the indexed
  attribute(s) are equality comparisons, and if the attribute selectivity is
  high. That means the number of distinct attribute values in relation to the 
  total number of documents should be high. This is the case for indexes
  declared `unique`. 

  The hash index should not be used if duplicate index values are allowed 
  (i.e. if the hash index is not declared `unique`) and it cannot be avoided
  that there will be many duplicate index values. For example, it should be
  avoided to use a hash index on an attribute with just 10 distinct values in a 
  collection with a million documents.

- skip list index: skip lists keep the indexed values in an order, so they can
  be used for equality and range queries. Skip list indexes will have a slightly
  higher overhead than hash indexes in case but they are more general and
  allow more use cases (e.g. range queries). Additionally, they can be used
  for lower selectivity attributes, when non-unique hash indexes are not a
  good fit.

- geo index: the geo index provided by ArangoDB allows searching for documents
  within a radius around a two-dimensional earth coordinate (point), or to
  find documents with are closest to a point. Document coordinates can either 
  be specified in two different document attributes or in a single attribute, e.g.

    { "latitude": 50.9406645, "longitude": 6.9599115 }

  or

    { "coords": [ 50.9406645, 6.9599115 ] }

- fulltext index: a fulltext index can be used to index all words contained in 
  a specific attribute of all documents in a collection. Only words with a 
  (specifiable) minimum length are indexed. Word tokenization is done using 
  the word boundary analysis provided by libicu, which is taking into account 
  the selected language provided at server start.

  The index supports complete match queries (full words) and prefix queries.

- cap constraint: the cap constraint provided by ArangoDB indexes documents
  not to speed up search queries, but to limit (cap) the number or size of
  documents in a collection.

Currently it is not possible to index system attributes in user-defined indexes.

!SECTION Working with Indexes

!SUBSECTION Collection Methods

`collection.index( index-handle)`

Returns the index with index-handle or null if no such index exists.

*Examples*

  arango> db.example.getIndexes().map(function(x) { return x.id; });
  ["example/0"]
  arango> db.example.index("93013/0");
  { "id" : "example/0", "type" : "primary", "fields" : ["_id"] }

returns information about the indexes

`getIndexes()`

Returns a list of all indexes defined for the collection.

*Examples*

  arango> db.demo.getIndexes()
  [
    {
      "id" : "demo/0",
      "type" : "primary",
      "fields" : [ "_id" ]
    },
    {
      "id" : "demo/2290971",
      "unique" : true,
      "type" : "hash",
      "fields" : [ "a" ]
    },
    {
      "id" : "demo/2946331",
      "unique" : false,
      "type" : "hash",
      "fields" : [ "b" ]
    },
    {
      "id" : "demo/3077403",
      "unique" : false,
      "type" : "skiplist",
      "fields" : [ "c" ]
    }
  ]

drops an index

`collection.dropIndex( index)`

Drops the index. If the index does not exist, then false is returned. If the index existed and was dropped, then true is returned. Note that you cannot drop some special indexes (e.g. the primary index of a collection or the edge index of an edge collection).

`collection.dropIndex( index-handle)`

Same as above. Instead of an index an index handle can be given.

*Examples*

  arango> db.example.ensureSkiplist("a", "b");
  { "id" : "example/991154", "unique" : false, "type" : "skiplist", "fields" : ["a", "b"], "isNewlyCreated" : true }

  arango> i = db.example.getIndexes();
  [
    { "id" : "example/0", "type" : "primary", "fields" : ["_id"] },
    { "id" : "example/991154", "unique" : false, "type" : "skiplist", "fields" : ["a", "b"] }
    ]

  arango> db.example.dropIndex(i[0])
  false

  arango> db.example.dropIndex(i[1].id)
  true

  arango> i = db.example.getIndexes();
  [{ "id" : "example/0", "type" : "primary", "fields" : ["_id"] }]

`collection.ensureIndex( index-description)`

Ensures that an index according to the index-description exists. A new index will be created if none exists with the given description.

The index-description must contain at least a type attribute. type can be one of the following values:

* hash: hash index
* skiplist: skiplist index
* fulltext: fulltext index
* bitarray: bitarray index
* geo1: geo index, with one attribute
* geo2: geo index, with two attributes
* cap: cap constraint

Other attributes may be necessary, depending on the index type.

Calling this method returns an index object. Whether or not the index object existed before the call is indicated in the return attribute isNewlyCreated.

*Examples*

  arango> db.example.ensureIndex({ type: "hash", fields: [ "name" ], unique: true });
  {
  "id" : "example/30242599562",
  "type" : "hash",
  "unique" : true,
  "fields" : [
  "name"
  ],
  "isNewlyCreated" : true
  }


<!--
@anchor HandlingIndexesRead
@copydetails JSF_ArangoCollection_prototype_index

@CLEARPAGE
@anchor HandlingIndexesReadAll
@copydetails JS_GetIndexesVocbaseCol

@CLEARPAGE
@anchor HandlingIndexesDelete
@copydetails JS_DropIndexVocbaseCol

@CLEARPAGE
@anchor HandlingIndexesEnsure
@copydetails JS_EnsureIndexVocbaseCol
-->

!SUBSECTION Database Methods

`db._index(index-handle)`

Returns the index with index-handle or null if no such index exists.

*Examples*

  arango> db.example.getIndexes().map(function(x) { return x.id; });
  ["example/0"]
  arango> db._index("example/0");
  { "id" : "example/0", "type" : "primary", "fields" : ["_id"] }

`db._dropIndex(index)`

Drops the index. If the index does not exists, then false is returned. If the index existed and was dropped, then true is returned. Note that you cannot drop the primary index.

`db._dropIndex(index-handle)`

Drops the index with index-handle.

*Examples*

  arango> db.example.ensureSkiplist("a", "b");
  { "id" : "example/1577138", "unique" : false, "type" : "skiplist", "fields" : ["a", "b"], "isNewlyCreated" : true }

  arango> i = db.example.getIndexes();
  [{ "id" : "example/0", "type" : "primary", "fields" : ["_id"] },
   { "id" : "example/1577138", "unique" : false, "type" : "skiplist", "fields" : ["a", "b"] }]

   arango> db._dropIndex(i[0]);
   false

   arango> db._dropIndex(i[1].id);
  true

  arango> i = db.example.getIndexes();
  [{ "id" : "example/0", "type" : "primary", "fields" : ["_id"] }]

<!--
@anchor HandlingIndexesDbRead
@copydetails JSF_ArangoDatabase_prototype__index

@CLEARPAGE
@anchor HandlingIndexesDbDelete
@copydetails JSF_ArangoDatabase_prototype__dropIndex
-->