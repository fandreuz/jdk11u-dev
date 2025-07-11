/*
 * Copyright (c) 1997, 2019, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#ifndef SHARE_VM_OOPS_INSTANCEKLASS_HPP
#define SHARE_VM_OOPS_INSTANCEKLASS_HPP

#include "classfile/classLoader.hpp"
#include "classfile/classLoaderData.hpp"
#include "classfile/moduleEntry.hpp"
#include "classfile/packageEntry.hpp"
#include "memory/referenceType.hpp"
#include "oops/annotations.hpp"
#include "oops/constMethod.hpp"
#include "oops/fieldInfo.hpp"
#include "oops/instanceOop.hpp"
#include "oops/klassVtable.hpp"
#include "runtime/handles.hpp"
#include "runtime/os.hpp"
#include "utilities/accessFlags.hpp"
#include "utilities/align.hpp"
#include "utilities/macros.hpp"
#if INCLUDE_JFR
#include "jfr/support/jfrKlassExtension.hpp"
#endif


// An InstanceKlass is the VM level representation of a Java class.
// It contains all information needed for at class at execution runtime.

//  InstanceKlass embedded field layout (after declared fields):
//    [EMBEDDED Java vtable             ] size in words = vtable_len
//    [EMBEDDED nonstatic oop-map blocks] size in words = nonstatic_oop_map_size
//      The embedded nonstatic oop-map blocks are short pairs (offset, length)
//      indicating where oops are located in instances of this klass.
//    [EMBEDDED implementor of the interface] only exist for interface
//    [EMBEDDED host klass        ] only exist for an anonymous class (JSR 292 enabled)
//    [EMBEDDED fingerprint       ] only if should_store_fingerprint()==true


// forward declaration for class -- see below for definition
#if INCLUDE_JVMTI
class BreakpointInfo;
#endif
class ClassFileParser;
class KlassDepChange;
class DependencyContext;
class fieldDescriptor;
class jniIdMapBase;
class JNIid;
class JvmtiCachedClassFieldMap;
class SuperTypeClosure;

// This is used in iterators below.
class FieldClosure: public StackObj {
public:
  virtual void do_field(fieldDescriptor* fd) = 0;
};

// Print fields.
// If "obj" argument to constructor is NULL, prints static fields, otherwise prints non-static fields.
class FieldPrinter: public FieldClosure {
   oop _obj;
   outputStream* _st;
 public:
   FieldPrinter(outputStream* st, oop obj = NULL) : _obj(obj), _st(st) {}
   void do_field(fieldDescriptor* fd);
};

// Describes where oops are located in instances of this klass.
class OopMapBlock {
 public:
  // Byte offset of the first oop mapped by this block.
  int offset() const          { return _offset; }
  void set_offset(int offset) { _offset = offset; }

  // Number of oops in this block.
  uint count() const         { return _count; }
  void set_count(uint count) { _count = count; }

  // sizeof(OopMapBlock) in words.
  static const int size_in_words() {
    return align_up((int)sizeof(OopMapBlock), wordSize) >>
      LogBytesPerWord;
  }

 private:
  int  _offset;
  uint _count;
};

struct JvmtiCachedClassFileData;

class InstanceKlass: public Klass {
  friend class VMStructs;
  friend class JVMCIVMStructs;
  friend class ClassFileParser;
  friend class CompileReplay;

 public:
  static const KlassID ID = InstanceKlassID;

 protected:
  InstanceKlass(const ClassFileParser& parser, unsigned kind, KlassID id = ID);

 public:
  InstanceKlass() { assert(DumpSharedSpaces || UseSharedSpaces, "only for CDS"); }

  // See "The Java Virtual Machine Specification" section 2.16.2-5 for a detailed description
  // of the class loading & initialization procedure, and the use of the states.
  enum ClassState {
    allocated,                          // allocated (but not yet linked)
    loaded,                             // loaded and inserted in class hierarchy (but not linked yet)
    linked,                             // successfully linked/verified (but not initialized yet)
    being_initialized,                  // currently running class initializer
    fully_initialized,                  // initialized (successfull final state)
    initialization_error                // error happened during initialization
  };

 private:
  static InstanceKlass* allocate_instance_klass(const ClassFileParser& parser, TRAPS);

 protected:
  // If you add a new field that points to any metaspace object, you
  // must add this field to InstanceKlass::metaspace_pointers_do().

  // Annotations for this class
  Annotations*    _annotations;
  // Package this class is defined in
  PackageEntry*   _package_entry;
  // Array classes holding elements of this class.
  Klass* volatile _array_klasses;
  // Constant pool for this class.
  ConstantPool* _constants;
  // The InnerClasses attribute and EnclosingMethod attribute. The
  // _inner_classes is an array of shorts. If the class has InnerClasses
  // attribute, then the _inner_classes array begins with 4-tuples of shorts
  // [inner_class_info_index, outer_class_info_index,
  // inner_name_index, inner_class_access_flags] for the InnerClasses
  // attribute. If the EnclosingMethod attribute exists, it occupies the
  // last two shorts [class_index, method_index] of the array. If only
  // the InnerClasses attribute exists, the _inner_classes array length is
  // number_of_inner_classes * 4. If the class has both InnerClasses
  // and EnclosingMethod attributes the _inner_classes array length is
  // number_of_inner_classes * 4 + enclosing_method_attribute_size.
  Array<jushort>* _inner_classes;

  // The NestMembers attribute. An array of shorts, where each is a
  // class info index for the class that is a nest member. This data
  // has not been validated.
  Array<jushort>* _nest_members;

  // The NestHost attribute. The class info index for the class
  // that is the nest-host of this class. This data has not been validated.
  jushort _nest_host_index;

  // Resolved nest-host klass: either true nest-host or self if we are not nested.
  // By always being set it makes nest-member access checks simpler.
  InstanceKlass* _nest_host;

  // the source debug extension for this klass, NULL if not specified.
  // Specified as UTF-8 string without terminating zero byte in the classfile,
  // it is stored in the instanceklass as a NULL-terminated UTF-8 string
  const char*     _source_debug_extension;
  // Array name derived from this class which needs unreferencing
  // if this class is unloaded.
  Symbol*         _array_name;

  // Number of heapOopSize words used by non-static fields in this klass
  // (including inherited fields but after header_size()).
  int             _nonstatic_field_size;
  int             _static_field_size;    // number words used by static fields (oop and non-oop) in this klass
  // Constant pool index to the utf8 entry of the Generic signature,
  // or 0 if none.
  u2              _generic_signature_index;
  // Constant pool index to the utf8 entry for the name of source file
  // containing this klass, 0 if not specified.
  u2              _source_file_name_index;
  u2              _static_oop_field_count;// number of static oop fields in this klass
  u2              _java_fields_count;    // The number of declared Java fields
  int             _nonstatic_oop_map_size;// size in words of nonstatic oop map blocks

  int             _itable_len;           // length of Java itable (in words)
  // _is_marked_dependent can be set concurrently, thus cannot be part of the
  // _misc_flags.
  bool            _is_marked_dependent;  // used for marking during flushing and deoptimization
  bool            _is_being_redefined;   // used for locking redefinition

  // The low two bits of _misc_flags contains the kind field.
  // This can be used to quickly discriminate among the four kinds of
  // InstanceKlass.

  static const unsigned _misc_kind_field_size = 2;
  static const unsigned _misc_kind_field_pos  = 0;
  static const unsigned _misc_kind_field_mask = (1u << _misc_kind_field_size) - 1u;

  static const unsigned _misc_kind_other        = 0; // concrete InstanceKlass
  static const unsigned _misc_kind_reference    = 1; // InstanceRefKlass
  static const unsigned _misc_kind_class_loader = 2; // InstanceClassLoaderKlass
  static const unsigned _misc_kind_mirror       = 3; // InstanceMirrorKlass

  // Start after _misc_kind field.
  enum {
    _misc_rewritten                           = 1 << 2,  // methods rewritten.
    _misc_has_nonstatic_fields                = 1 << 3,  // for sizing with UseCompressedOops
    _misc_should_verify_class                 = 1 << 4,  // allow caching of preverification
    _misc_is_anonymous                        = 1 << 5,  // has embedded _host_klass field
    _misc_is_contended                        = 1 << 6,  // marked with contended annotation
    _misc_has_nonstatic_concrete_methods      = 1 << 7,  // class/superclass/implemented interfaces has non-static, concrete methods
    _misc_declares_nonstatic_concrete_methods = 1 << 8,  // directly declares non-static, concrete methods
    _misc_has_been_redefined                  = 1 << 9,  // class has been redefined
    _misc_has_passed_fingerprint_check        = 1 << 10, // when this class was loaded, the fingerprint computed from its
                                                         // code source was found to be matching the value recorded by AOT.
    _misc_is_scratch_class                    = 1 << 11, // class is the redefined scratch class
    _misc_is_shared_boot_class                = 1 << 12, // defining class loader is boot class loader
    _misc_is_shared_platform_class            = 1 << 13, // defining class loader is platform class loader
    _misc_is_shared_app_class                 = 1 << 14, // defining class loader is app class loader
    _misc_has_resolved_methods                = 1 << 15  // resolved methods table entries added for this class
  };
  u2 loader_type_bits() {
    return _misc_is_shared_boot_class|_misc_is_shared_platform_class|_misc_is_shared_app_class;
  }
  u2              _misc_flags;
  u2              _minor_version;        // minor version number of class file
  u2              _major_version;        // major version number of class file
  Thread*         _init_thread;          // Pointer to current thread doing initialization (to handle recursive initialization)
  OopMapCache*    volatile _oop_map_cache;   // OopMapCache for all methods in the klass (allocated lazily)
  JNIid*          _jni_ids;              // First JNI identifier for static fields in this class
  jmethodID*      volatile _methods_jmethod_ids;  // jmethodIDs corresponding to method_idnum, or NULL if none
  intptr_t        _dep_context;          // packed DependencyContext structure
  nmethod*        _osr_nmethods_head;    // Head of list of on-stack replacement nmethods for this class
#if INCLUDE_JVMTI
  BreakpointInfo* _breakpoints;          // bpt lists, managed by Method*
  // Linked instanceKlasses of previous versions
  InstanceKlass* _previous_versions;
  // JVMTI fields can be moved to their own structure - see 6315920
  // JVMTI: cached class file, before retransformable agent modified it in CFLH
  JvmtiCachedClassFileData* _cached_class_file;
#endif

  volatile u2     _idnum_allocated_count;         // JNI/JVMTI: increments with the addition of methods, old ids don't change

  // Class states are defined as ClassState (see above).
  // Place the _init_state here to utilize the unused 2-byte after
  // _idnum_allocated_count.
  u1              _init_state;                    // state of class
  u1              _reference_type;                // reference type

  u2              _this_class_index;              // constant pool entry
#if INCLUDE_JVMTI
  JvmtiCachedClassFieldMap* _jvmti_cached_class_field_map;  // JVMTI: used during heap iteration
#endif

  NOT_PRODUCT(int _verify_count;)  // to avoid redundant verifies

  // Method array.
  Array<Method*>* _methods;
  // Default Method Array, concrete methods inherited from interfaces
  Array<Method*>* _default_methods;
  // Interface (Klass*s) this class declares locally to implement.
  Array<Klass*>* _local_interfaces;
  // Interface (Klass*s) this class implements transitively.
  Array<Klass*>* _transitive_interfaces;
  // Int array containing the original order of method in the class file (for JVMTI).
  Array<int>*     _method_ordering;
  // Int array containing the vtable_indices for default_methods
  // offset matches _default_methods offset
  Array<int>*     _default_vtable_indices;

  // Instance and static variable information, starts with 6-tuples of shorts
  // [access, name index, sig index, initval index, low_offset, high_offset]
  // for all fields, followed by the generic signature data at the end of
  // the array. Only fields with generic signature attributes have the generic
  // signature data set in the array. The fields array looks like following:
  //
  // f1: [access, name index, sig index, initial value index, low_offset, high_offset]
  // f2: [access, name index, sig index, initial value index, low_offset, high_offset]
  //      ...
  // fn: [access, name index, sig index, initial value index, low_offset, high_offset]
  //     [generic signature index]
  //     [generic signature index]
  //     ...
  Array<u2>*      _fields;

  // embedded Java vtable follows here
  // embedded Java itables follows here
  // embedded static fields follows here
  // embedded nonstatic oop-map blocks follows here
  // embedded implementor of this interface follows here
  //   The embedded implementor only exists if the current klass is an
  //   iterface. The possible values of the implementor fall into following
  //   three cases:
  //     NULL: no implementor.
  //     A Klass* that's not itself: one implementor.
  //     Itself: more than one implementors.
  // embedded host klass follows here
  //   The embedded host klass only exists in an anonymous class for
  //   dynamic language support (JSR 292 enabled). The host class grants
  //   its access privileges to this class also. The host class is either
  //   named, or a previously loaded anonymous class. A non-anonymous class
  //   or an anonymous class loaded through normal classloading does not
  //   have this embedded field.
  //

  friend class SystemDictionary;

 public:
  u2 loader_type() {
    return _misc_flags & loader_type_bits();
  }

  bool is_shared_boot_class() const {
    return (_misc_flags & _misc_is_shared_boot_class) != 0;
  }
  bool is_shared_platform_class() const {
    return (_misc_flags & _misc_is_shared_platform_class) != 0;
  }
  bool is_shared_app_class() const {
    return (_misc_flags & _misc_is_shared_app_class) != 0;
  }

  void clear_class_loader_type() {
    _misc_flags &= ~loader_type_bits();
  }

  void set_class_loader_type(s2 loader_type) {
    switch (loader_type) {
    case ClassLoader::BOOT_LOADER:
      _misc_flags |= _misc_is_shared_boot_class;
       break;
    case ClassLoader::PLATFORM_LOADER:
      _misc_flags |= _misc_is_shared_platform_class;
      break;
    case ClassLoader::APP_LOADER:
      _misc_flags |= _misc_is_shared_app_class;
      break;
    default:
      ShouldNotReachHere();
      break;
    }
  }

  bool has_nonstatic_fields() const        {
    return (_misc_flags & _misc_has_nonstatic_fields) != 0;
  }
  void set_has_nonstatic_fields(bool b)    {
    if (b) {
      _misc_flags |= _misc_has_nonstatic_fields;
    } else {
      _misc_flags &= ~_misc_has_nonstatic_fields;
    }
  }

  // field sizes
  int nonstatic_field_size() const         { return _nonstatic_field_size; }
  void set_nonstatic_field_size(int size)  { _nonstatic_field_size = size; }

  int static_field_size() const            { return _static_field_size; }
  void set_static_field_size(int size)     { _static_field_size = size; }

  int static_oop_field_count() const       { return (int)_static_oop_field_count; }
  void set_static_oop_field_count(u2 size) { _static_oop_field_count = size; }

  // Java itable
  int  itable_length() const               { return _itable_len; }
  void set_itable_length(int len)          { _itable_len = len; }

  // array klasses
  Klass* array_klasses() const             { return _array_klasses; }
  inline Klass* array_klasses_acquire() const; // load with acquire semantics
  void set_array_klasses(Klass* k)         { _array_klasses = k; }
  inline void release_set_array_klasses(Klass* k); // store with release semantics

  // methods
  Array<Method*>* methods() const          { return _methods; }
  void set_methods(Array<Method*>* a)      { _methods = a; }
  Method* method_with_idnum(int idnum);
  Method* method_with_orig_idnum(int idnum);
  Method* method_with_orig_idnum(int idnum, int version);

  // method ordering
  Array<int>* method_ordering() const     { return _method_ordering; }
  void set_method_ordering(Array<int>* m) { _method_ordering = m; }
  void copy_method_ordering(const intArray* m, TRAPS);

  // default_methods
  Array<Method*>* default_methods() const  { return _default_methods; }
  void set_default_methods(Array<Method*>* a) { _default_methods = a; }

  // default method vtable_indices
  Array<int>* default_vtable_indices() const { return _default_vtable_indices; }
  void set_default_vtable_indices(Array<int>* v) { _default_vtable_indices = v; }
  Array<int>* create_new_default_vtable_indices(int len, TRAPS);

  // interfaces
  Array<Klass*>* local_interfaces() const          { return _local_interfaces; }
  void set_local_interfaces(Array<Klass*>* a)      {
    guarantee(_local_interfaces == NULL || a == NULL, "Just checking");
    _local_interfaces = a; }

  Array<Klass*>* transitive_interfaces() const     { return _transitive_interfaces; }
  void set_transitive_interfaces(Array<Klass*>* a) {
    guarantee(_transitive_interfaces == NULL || a == NULL, "Just checking");
    _transitive_interfaces = a;
  }

 private:
  friend class fieldDescriptor;
  FieldInfo* field(int index) const { return FieldInfo::from_field_array(_fields, index); }

 public:
  int     field_offset      (int index) const { return field(index)->offset(); }
  int     field_access_flags(int index) const { return field(index)->access_flags(); }
  Symbol* field_name        (int index) const { return field(index)->name(constants()); }
  Symbol* field_signature   (int index) const { return field(index)->signature(constants()); }

  // Number of Java declared fields
  int java_fields_count() const           { return (int)_java_fields_count; }

  Array<u2>* fields() const            { return _fields; }
  void set_fields(Array<u2>* f, u2 java_fields_count) {
    guarantee(_fields == NULL || f == NULL, "Just checking");
    _fields = f;
    _java_fields_count = java_fields_count;
  }

  // inner classes
  Array<u2>* inner_classes() const       { return _inner_classes; }
  void set_inner_classes(Array<u2>* f)   { _inner_classes = f; }

  // nest members
  Array<u2>* nest_members() const     { return _nest_members; }
  void set_nest_members(Array<u2>* m) { _nest_members = m; }

  // nest-host index
  jushort nest_host_index() const { return _nest_host_index; }
  void set_nest_host_index(u2 i)  { _nest_host_index = i; }

private:
  // Called to verify that k is a member of this nest - does not look at k's nest-host
  bool has_nest_member(InstanceKlass* k, TRAPS) const;
public:
  // Returns nest-host class, resolving and validating it if needed
  // Returns NULL if an exception occurs during loading, or validation fails
  InstanceKlass* nest_host(Symbol* validationException, TRAPS);
  // Check if this klass is a nestmate of k - resolves this nest-host and k's
  bool has_nestmate_access_to(InstanceKlass* k, TRAPS);

  enum InnerClassAttributeOffset {
    // From http://mirror.eng/products/jdk/1.1/docs/guide/innerclasses/spec/innerclasses.doc10.html#18814
    inner_class_inner_class_info_offset = 0,
    inner_class_outer_class_info_offset = 1,
    inner_class_inner_name_offset = 2,
    inner_class_access_flags_offset = 3,
    inner_class_next_offset = 4
  };

  enum EnclosingMethodAttributeOffset {
    enclosing_method_class_index_offset = 0,
    enclosing_method_method_index_offset = 1,
    enclosing_method_attribute_size = 2
  };

  // method override check
  bool is_override(const methodHandle& super_method, Handle targetclassloader, Symbol* targetclassname, TRAPS);

  // package
  PackageEntry* package() const     { return _package_entry; }
  ModuleEntry* module() const;
  bool in_unnamed_package() const   { return (_package_entry == NULL); }
  void set_package(PackageEntry* p) { _package_entry = p; }
  void set_package(ClassLoaderData* loader_data, TRAPS);
  bool is_same_class_package(const Klass* class2) const;
  bool is_same_class_package(oop other_class_loader, const Symbol* other_class_name) const;

  // find an enclosing class
  InstanceKlass* compute_enclosing_class(bool* inner_is_member, TRAPS) const;

  // Find InnerClasses attribute and return outer_class_info_index & inner_name_index.
  bool find_inner_classes_attr(int* ooff, int* noff, TRAPS) const;

 private:
  // Check prohibited package ("java/" only loadable by boot or platform loaders)
  static void check_prohibited_package(Symbol* class_name,
                                       ClassLoaderData* loader_data,
                                       TRAPS);
 public:
  // tell if two classes have the same enclosing class (at package level)
  bool is_same_package_member(const Klass* class2, TRAPS) const;

  // initialization state
  bool is_loaded() const                   { return _init_state >= loaded; }
  bool is_linked() const                   { return _init_state >= linked; }
  bool is_initialized() const              { return _init_state == fully_initialized; }
  bool is_not_initialized() const          { return _init_state <  being_initialized; }
  bool is_being_initialized() const        { return _init_state == being_initialized; }
  bool is_in_error_state() const           { return _init_state == initialization_error; }
  bool is_reentrant_initialization(Thread *thread)  { return thread == _init_thread; }
  ClassState  init_state()                 { return (ClassState)_init_state; }
  bool is_rewritten() const                { return (_misc_flags & _misc_rewritten) != 0; }

  // defineClass specified verification
  bool should_verify_class() const         {
    return (_misc_flags & _misc_should_verify_class) != 0;
  }
  void set_should_verify_class(bool value) {
    if (value) {
      _misc_flags |= _misc_should_verify_class;
    } else {
      _misc_flags &= ~_misc_should_verify_class;
    }
  }

  // marking
  bool is_marked_dependent() const         { return _is_marked_dependent; }
  void set_is_marked_dependent(bool value) { _is_marked_dependent = value; }

  // initialization (virtuals from Klass)
  bool should_be_initialized() const;  // means that initialize should be called
  void initialize(TRAPS);
  void link_class(TRAPS);
  bool link_class_or_fail(TRAPS); // returns false on failure
  void rewrite_class(TRAPS);
  void link_methods(TRAPS);
  Method* class_initializer() const;

  // set the class to initialized if no static initializer is present
  void eager_initialize(Thread *thread);

  // reference type
  ReferenceType reference_type() const     { return (ReferenceType)_reference_type; }
  void set_reference_type(ReferenceType t) {
    assert(t == (u1)t, "overflow");
    _reference_type = (u1)t;
  }

  // this class cp index
  u2 this_class_index() const             { return _this_class_index; }
  void set_this_class_index(u2 index)     { _this_class_index = index; }

  static ByteSize reference_type_offset() { return in_ByteSize(offset_of(InstanceKlass, _reference_type)); }

  // find local field, returns true if found
  bool find_local_field(Symbol* name, Symbol* sig, fieldDescriptor* fd) const;
  // find field in direct superinterfaces, returns the interface in which the field is defined
  Klass* find_interface_field(Symbol* name, Symbol* sig, fieldDescriptor* fd) const;
  // find field according to JVM spec 5.4.3.2, returns the klass in which the field is defined
  Klass* find_field(Symbol* name, Symbol* sig, fieldDescriptor* fd) const;
  // find instance or static fields according to JVM spec 5.4.3.2, returns the klass in which the field is defined
  Klass* find_field(Symbol* name, Symbol* sig, bool is_static, fieldDescriptor* fd) const;

  // find a non-static or static field given its offset within the class.
  bool contains_field_offset(int offset) {
    return instanceOopDesc::contains_field_offset(offset, nonstatic_field_size());
  }

  bool find_local_field_from_offset(int offset, bool is_static, fieldDescriptor* fd) const;
  bool find_field_from_offset(int offset, bool is_static, fieldDescriptor* fd) const;

  // find a local method (returns NULL if not found)
  Method* find_method(const Symbol* name, const Symbol* signature) const;
  static Method* find_method(const Array<Method*>* methods,
                             const Symbol* name,
                             const Symbol* signature);

  // find a local method, but skip static methods
  Method* find_instance_method(const Symbol* name, const Symbol* signature,
                               PrivateLookupMode private_mode) const;
  static Method* find_instance_method(const Array<Method*>* methods,
                                      const Symbol* name,
                                      const Symbol* signature,
                                      PrivateLookupMode private_mode);

  // find a local method (returns NULL if not found)
  Method* find_local_method(const Symbol* name,
                            const Symbol* signature,
                            OverpassLookupMode overpass_mode,
                            StaticLookupMode static_mode,
                            PrivateLookupMode private_mode) const;

  // find a local method from given methods array (returns NULL if not found)
  static Method* find_local_method(const Array<Method*>* methods,
                                   const Symbol* name,
                                   const Symbol* signature,
                                   OverpassLookupMode overpass_mode,
                                   StaticLookupMode static_mode,
                                   PrivateLookupMode private_mode);

  // find a local method index in methods or default_methods (returns -1 if not found)
  static int find_method_index(const Array<Method*>* methods,
                               const Symbol* name,
                               const Symbol* signature,
                               OverpassLookupMode overpass_mode,
                               StaticLookupMode static_mode,
                               PrivateLookupMode private_mode);

  // lookup operation (returns NULL if not found)
  Method* uncached_lookup_method(const Symbol* name,
                                 const Symbol* signature,
                                 OverpassLookupMode overpass_mode,
                                 PrivateLookupMode private_mode = find_private) const;

  // lookup a method in all the interfaces that this class implements
  // (returns NULL if not found)
  Method* lookup_method_in_all_interfaces(Symbol* name, Symbol* signature, DefaultsLookupMode defaults_mode) const;

  // lookup a method in local defaults then in all interfaces
  // (returns NULL if not found)
  Method* lookup_method_in_ordered_interfaces(Symbol* name, Symbol* signature) const;

  // Find method indices by name.  If a method with the specified name is
  // found the index to the first method is returned, and 'end' is filled in
  // with the index of first non-name-matching method.  If no method is found
  // -1 is returned.
  int find_method_by_name(const Symbol* name, int* end) const;
  static int find_method_by_name(const Array<Method*>* methods,
                                 const Symbol* name, int* end);

  // constant pool
  ConstantPool* constants() const        { return _constants; }
  void set_constants(ConstantPool* c)    { _constants = c; }

  // protection domain
  oop protection_domain() const;

  // signers
  objArrayOop signers() const;

  // host class
  InstanceKlass* host_klass() const              {
    InstanceKlass** hk = adr_host_klass();
    if (hk == NULL) {
      assert(!is_anonymous(), "Anonymous classes have host klasses");
      return NULL;
    } else {
      assert(*hk != NULL, "host klass should always be set if the address is not null");
      assert(is_anonymous(), "Only anonymous classes have host klasses");
      return *hk;
    }
  }
  void set_host_klass(const InstanceKlass* host) {
    assert(is_anonymous(), "not anonymous");
    const InstanceKlass** addr = (const InstanceKlass **)adr_host_klass();
    assert(addr != NULL, "no reversed space");
    if (addr != NULL) {
      *addr = host;
    }
  }
  bool has_host_klass() const              {
    return adr_host_klass() != NULL;
  }
  bool is_anonymous() const                {
    return (_misc_flags & _misc_is_anonymous) != 0;
  }
  void set_is_anonymous(bool value)        {
    if (value) {
      _misc_flags |= _misc_is_anonymous;
    } else {
      _misc_flags &= ~_misc_is_anonymous;
    }
  }

  bool is_contended() const                {
    return (_misc_flags & _misc_is_contended) != 0;
  }
  void set_is_contended(bool value)        {
    if (value) {
      _misc_flags |= _misc_is_contended;
    } else {
      _misc_flags &= ~_misc_is_contended;
    }
  }

  // source file name
  Symbol* source_file_name() const               {
    return (_source_file_name_index == 0) ?
      (Symbol*)NULL : _constants->symbol_at(_source_file_name_index);
  }
  u2 source_file_name_index() const              {
    return _source_file_name_index;
  }
  void set_source_file_name_index(u2 sourcefile_index) {
    _source_file_name_index = sourcefile_index;
  }

  // minor and major version numbers of class file
  u2 minor_version() const                 { return _minor_version; }
  void set_minor_version(u2 minor_version) { _minor_version = minor_version; }
  u2 major_version() const                 { return _major_version; }
  void set_major_version(u2 major_version) { _major_version = major_version; }

  // source debug extension
  const char* source_debug_extension() const { return _source_debug_extension; }
  void set_source_debug_extension(const char* array, int length);

  // symbol unloading support (refcount already added)
  Symbol* array_name()                     { return _array_name; }
  void set_array_name(Symbol* name)        { assert(_array_name == NULL  || name == NULL, "name already created"); _array_name = name; }

  // nonstatic oop-map blocks
  static int nonstatic_oop_map_size(unsigned int oop_map_count) {
    return oop_map_count * OopMapBlock::size_in_words();
  }
  unsigned int nonstatic_oop_map_count() const {
    return _nonstatic_oop_map_size / OopMapBlock::size_in_words();
  }
  int nonstatic_oop_map_size() const { return _nonstatic_oop_map_size; }
  void set_nonstatic_oop_map_size(int words) {
    _nonstatic_oop_map_size = words;
  }

#if INCLUDE_JVMTI
  // The flag is in access_flags so that it can be set and reset using atomic
  // operations, and not be reset by other misc_flag settings.
  bool is_being_redefined() const          {
    return _access_flags.is_being_redefined();
  }
  void set_is_being_redefined(bool value)  {
    if (value) {
      _access_flags.set_is_being_redefined();
    } else {
      _access_flags.clear_is_being_redefined();
    }
  }

  // RedefineClasses() support for previous versions:
  void add_previous_version(InstanceKlass* ik, int emcp_method_count);
  void purge_previous_version_list();

  InstanceKlass* previous_versions() const { return _previous_versions; }
#else
  InstanceKlass* previous_versions() const { return NULL; }
#endif

  InstanceKlass* get_klass_version(int version) {
    for (InstanceKlass* ik = this; ik != NULL; ik = ik->previous_versions()) {
      if (ik->constants()->version() == version) {
        return ik;
      }
    }
    return NULL;
  }

  bool has_been_redefined() const {
    return (_misc_flags & _misc_has_been_redefined) != 0;
  }
  void set_has_been_redefined() {
    _misc_flags |= _misc_has_been_redefined;
  }

  bool has_passed_fingerprint_check() const {
    return (_misc_flags & _misc_has_passed_fingerprint_check) != 0;
  }
  void set_has_passed_fingerprint_check(bool b) {
    if (b) {
      _misc_flags |= _misc_has_passed_fingerprint_check;
    } else {
      _misc_flags &= ~_misc_has_passed_fingerprint_check;
    }
  }
  bool supers_have_passed_fingerprint_checks();

  static bool should_store_fingerprint(bool is_anonymous);
  bool should_store_fingerprint() const { return should_store_fingerprint(is_anonymous()); }
  bool has_stored_fingerprint() const;
  uint64_t get_stored_fingerprint() const;
  void store_fingerprint(uint64_t fingerprint);

  bool is_scratch_class() const {
    return (_misc_flags & _misc_is_scratch_class) != 0;
  }

  void set_is_scratch_class() {
    _misc_flags |= _misc_is_scratch_class;
  }

  bool has_resolved_methods() const {
    return (_misc_flags & _misc_has_resolved_methods) != 0;
  }

  void set_has_resolved_methods() {
    _misc_flags |= _misc_has_resolved_methods;
  }
private:

  void set_kind(unsigned kind) {
    assert(kind <= _misc_kind_field_mask, "Invalid InstanceKlass kind");
    unsigned fmask = _misc_kind_field_mask << _misc_kind_field_pos;
    unsigned flags = _misc_flags & ~fmask;
    _misc_flags = (flags | (kind << _misc_kind_field_pos));
  }

  bool is_kind(unsigned desired) const {
    unsigned kind = (_misc_flags >> _misc_kind_field_pos) & _misc_kind_field_mask;
    return kind == desired;
  }

public:

  // Other is anything that is not one of the more specialized kinds of InstanceKlass.
  bool is_other_instance_klass() const        { return is_kind(_misc_kind_other); }
  bool is_reference_instance_klass() const    { return is_kind(_misc_kind_reference); }
  bool is_mirror_instance_klass() const       { return is_kind(_misc_kind_mirror); }
  bool is_class_loader_instance_klass() const { return is_kind(_misc_kind_class_loader); }

#if INCLUDE_JVMTI

  void init_previous_versions() {
    _previous_versions = NULL;
  }

 private:
  static bool  _has_previous_versions;
 public:
  static void purge_previous_versions(InstanceKlass* ik) {
    if (ik->has_been_redefined()) {
      ik->purge_previous_version_list();
    }
  }

  static bool has_previous_versions_and_reset();

  // JVMTI: Support for caching a class file before it is modified by an agent that can do retransformation
  void set_cached_class_file(JvmtiCachedClassFileData *data) {
    _cached_class_file = data;
  }
  JvmtiCachedClassFileData * get_cached_class_file();
  jint get_cached_class_file_len();
  unsigned char * get_cached_class_file_bytes();

  // JVMTI: Support for caching of field indices, types, and offsets
  void set_jvmti_cached_class_field_map(JvmtiCachedClassFieldMap* descriptor) {
    _jvmti_cached_class_field_map = descriptor;
  }
  JvmtiCachedClassFieldMap* jvmti_cached_class_field_map() const {
    return _jvmti_cached_class_field_map;
  }
#else // INCLUDE_JVMTI

  static void purge_previous_versions(InstanceKlass* ik) { return; };
  static bool has_previous_versions_and_reset() { return false; }

  void set_cached_class_file(JvmtiCachedClassFileData *data) {
    assert(data == NULL, "unexpected call with JVMTI disabled");
  }
  JvmtiCachedClassFileData * get_cached_class_file() { return (JvmtiCachedClassFileData *)NULL; }

#endif // INCLUDE_JVMTI

  bool has_nonstatic_concrete_methods() const {
    return (_misc_flags & _misc_has_nonstatic_concrete_methods) != 0;
  }
  void set_has_nonstatic_concrete_methods(bool b) {
    if (b) {
      _misc_flags |= _misc_has_nonstatic_concrete_methods;
    } else {
      _misc_flags &= ~_misc_has_nonstatic_concrete_methods;
    }
  }

  bool declares_nonstatic_concrete_methods() const {
    return (_misc_flags & _misc_declares_nonstatic_concrete_methods) != 0;
  }
  void set_declares_nonstatic_concrete_methods(bool b) {
    if (b) {
      _misc_flags |= _misc_declares_nonstatic_concrete_methods;
    } else {
      _misc_flags &= ~_misc_declares_nonstatic_concrete_methods;
    }
  }

  // for adding methods, ConstMethod::UNSET_IDNUM means no more ids available
  inline u2 next_method_idnum();
  void set_initial_method_idnum(u2 value)             { _idnum_allocated_count = value; }

  // generics support
  Symbol* generic_signature() const                   {
    return (_generic_signature_index == 0) ?
      (Symbol*)NULL : _constants->symbol_at(_generic_signature_index);
  }
  u2 generic_signature_index() const                  {
    return _generic_signature_index;
  }
  void set_generic_signature_index(u2 sig_index)      {
    _generic_signature_index = sig_index;
  }

  u2 enclosing_method_data(int offset) const;
  u2 enclosing_method_class_index() const {
    return enclosing_method_data(enclosing_method_class_index_offset);
  }
  u2 enclosing_method_method_index() {
    return enclosing_method_data(enclosing_method_method_index_offset);
  }
  void set_enclosing_method_indices(u2 class_index,
                                    u2 method_index);

  // jmethodID support
  jmethodID get_jmethod_id(const methodHandle& method_h);
  jmethodID get_jmethod_id_fetch_or_update(size_t idnum,
                     jmethodID new_id, jmethodID* new_jmeths,
                     jmethodID* to_dealloc_id_p,
                     jmethodID** to_dealloc_jmeths_p);
  static void get_jmethod_id_length_value(jmethodID* cache, size_t idnum,
                size_t *length_p, jmethodID* id_p);
  void ensure_space_for_methodids(int start_offset = 0);
  jmethodID jmethod_id_or_null(Method* method);

  // annotations support
  Annotations* annotations() const          { return _annotations; }
  void set_annotations(Annotations* anno)   { _annotations = anno; }

  AnnotationArray* class_annotations() const {
    return (_annotations != NULL) ? _annotations->class_annotations() : NULL;
  }
  Array<AnnotationArray*>* fields_annotations() const {
    return (_annotations != NULL) ? _annotations->fields_annotations() : NULL;
  }
  AnnotationArray* class_type_annotations() const {
    return (_annotations != NULL) ? _annotations->class_type_annotations() : NULL;
  }
  Array<AnnotationArray*>* fields_type_annotations() const {
    return (_annotations != NULL) ? _annotations->fields_type_annotations() : NULL;
  }
  // allocation
  instanceOop allocate_instance(TRAPS);

  // additional member function to return a handle
  instanceHandle allocate_instance_handle(TRAPS);

  objArrayOop allocate_objArray(int n, int length, TRAPS);
  // Helper function
  static instanceOop register_finalizer(instanceOop i, TRAPS);

  // Check whether reflection/jni/jvm code is allowed to instantiate this class;
  // if not, throw either an Error or an Exception.
  virtual void check_valid_for_instantiation(bool throwError, TRAPS);

  // initialization
  void call_class_initializer(TRAPS);
  void set_initialization_state_and_notify(ClassState state, TRAPS);

  // OopMapCache support
  OopMapCache* oop_map_cache()               { return _oop_map_cache; }
  void set_oop_map_cache(OopMapCache *cache) { _oop_map_cache = cache; }
  void mask_for(const methodHandle& method, int bci, InterpreterOopMap* entry);

  // JNI identifier support (for static fields - for jni performance)
  JNIid* jni_ids()                               { return _jni_ids; }
  void set_jni_ids(JNIid* ids)                   { _jni_ids = ids; }
  JNIid* jni_id_for(int offset);

  // maintenance of deoptimization dependencies
  inline DependencyContext dependencies();
  int  mark_dependent_nmethods(KlassDepChange& changes);
  void add_dependent_nmethod(nmethod* nm);
  void remove_dependent_nmethod(nmethod* nm, bool delete_immediately);

  // On-stack replacement support
  nmethod* osr_nmethods_head() const         { return _osr_nmethods_head; };
  void set_osr_nmethods_head(nmethod* h)     { _osr_nmethods_head = h; };
  void add_osr_nmethod(nmethod* n);
  bool remove_osr_nmethod(nmethod* n);
  int mark_osr_nmethods(const Method* m);
  nmethod* lookup_osr_nmethod(const Method* m, int bci, int level, bool match_level) const;

#if INCLUDE_JVMTI
  // Breakpoint support (see methods on Method* for details)
  BreakpointInfo* breakpoints() const       { return _breakpoints; };
  void set_breakpoints(BreakpointInfo* bps) { _breakpoints = bps; };
#endif

  // support for stub routines
  static ByteSize init_state_offset()  { return in_ByteSize(offset_of(InstanceKlass, _init_state)); }
  JFR_ONLY(DEFINE_KLASS_TRACE_ID_OFFSET;)
  static ByteSize init_thread_offset() { return in_ByteSize(offset_of(InstanceKlass, _init_thread)); }

  // subclass/subinterface checks
  bool implements_interface(Klass* k) const;
  bool is_same_or_direct_interface(Klass* k) const;

#ifdef ASSERT
  // check whether this class or one of its superclasses was redefined
  bool has_redefined_this_or_super() const;
#endif

  // Access to the implementor of an interface.
  Klass* implementor() const;
  void set_implementor(Klass* k);
  int  nof_implementors() const;
  void add_implementor(Klass* k);  // k is a new class that implements this interface
  void init_implementor();           // initialize

  // link this class into the implementors list of every interface it implements
  void process_interfaces(Thread *thread);

  // virtual operations from Klass
  bool is_leaf_class() const               { return _subklass == NULL; }
  GrowableArray<Klass*>* compute_secondary_supers(int num_extra_slots,
                                                  Array<Klass*>* transitive_interfaces);
  bool compute_is_subtype_of(Klass* k);
  bool can_be_primary_super_slow() const;
  int oop_size(oop obj)  const             { return size_helper(); }
  // slow because it's a virtual call and used for verifying the layout_helper.
  // Using the layout_helper bits, we can call is_instance_klass without a virtual call.
  DEBUG_ONLY(bool is_instance_klass_slow() const      { return true; })

  // Iterators
  void do_local_static_fields(FieldClosure* cl);
  void do_nonstatic_fields(FieldClosure* cl); // including inherited fields
  void do_local_static_fields(void f(fieldDescriptor*, Handle, TRAPS), Handle, TRAPS);

  void methods_do(void f(Method* method));
  void array_klasses_do(void f(Klass* k));
  void array_klasses_do(void f(Klass* k, TRAPS), TRAPS);
  bool super_types_do(SuperTypeClosure* blk);

  static InstanceKlass* cast(Klass* k) {
    return const_cast<InstanceKlass*>(cast(const_cast<const Klass*>(k)));
  }

  static const InstanceKlass* cast(const Klass* k) {
    assert(k != NULL, "k should not be null");
    assert(k->is_instance_klass(), "cast to InstanceKlass");
    return static_cast<const InstanceKlass*>(k);
  }

  InstanceKlass* java_super() const {
    return (super() == NULL) ? NULL : cast(super());
  }

  // Sizing (in words)
  static int header_size()            { return sizeof(InstanceKlass)/wordSize; }

  static int size(int vtable_length, int itable_length,
                  int nonstatic_oop_map_size,
                  bool is_interface, bool is_anonymous, bool has_stored_fingerprint) {
    return align_metadata_size(header_size() +
           vtable_length +
           itable_length +
           nonstatic_oop_map_size +
           (is_interface ? (int)sizeof(Klass*)/wordSize : 0) +
           (is_anonymous ? (int)sizeof(Klass*)/wordSize : 0) +
           (has_stored_fingerprint ? (int)sizeof(uint64_t*)/wordSize : 0));
  }
  int size() const                    { return size(vtable_length(),
                                               itable_length(),
                                               nonstatic_oop_map_size(),
                                               is_interface(),
                                               is_anonymous(),
                                               has_stored_fingerprint());
  }
#if INCLUDE_SERVICES
  virtual void collect_statistics(KlassSizeStats *sz) const;
#endif

  intptr_t* start_of_itable()   const { return (intptr_t*)start_of_vtable() + vtable_length(); }
  intptr_t* end_of_itable()     const { return start_of_itable() + itable_length(); }

  int  itable_offset_in_words() const { return start_of_itable() - (intptr_t*)this; }

  oop static_field_base_raw() { return java_mirror(); }

  OopMapBlock* start_of_nonstatic_oop_maps() const {
    return (OopMapBlock*)(start_of_itable() + itable_length());
  }

  Klass** end_of_nonstatic_oop_maps() const {
    return (Klass**)(start_of_nonstatic_oop_maps() +
                     nonstatic_oop_map_count());
  }

  Klass** adr_implementor() const {
    if (is_interface()) {
      return (Klass**)end_of_nonstatic_oop_maps();
    } else {
      return NULL;
    }
  };

  InstanceKlass** adr_host_klass() const {
    if (is_anonymous()) {
      InstanceKlass** adr_impl = (InstanceKlass **)adr_implementor();
      if (adr_impl != NULL) {
        return adr_impl + 1;
      } else {
        return (InstanceKlass **)end_of_nonstatic_oop_maps();
      }
    } else {
      return NULL;
    }
  }

  address adr_fingerprint() const {
    if (has_stored_fingerprint()) {
      InstanceKlass** adr_host = adr_host_klass();
      if (adr_host != NULL) {
        return (address)(adr_host + 1);
      }

      Klass** adr_impl = adr_implementor();
      if (adr_impl != NULL) {
        return (address)(adr_impl + 1);
      }

      return (address)end_of_nonstatic_oop_maps();
    } else {
      return NULL;
    }
  }

  // Use this to return the size of an instance in heap words:
  int size_helper() const {
    return layout_helper_to_size_helper(layout_helper());
  }

  // This bit is initialized in classFileParser.cpp.
  // It is false under any of the following conditions:
  //  - the class is abstract (including any interface)
  //  - the class has a finalizer (if !RegisterFinalizersAtInit)
  //  - the class size is larger than FastAllocateSizeLimit
  //  - the class is java/lang/Class, which cannot be allocated directly
  bool can_be_fastpath_allocated() const {
    return !layout_helper_needs_slow_path(layout_helper());
  }

  // Java itable
  klassItable itable() const;        // return klassItable wrapper
  Method* method_at_itable(Klass* holder, int index, TRAPS);

#if INCLUDE_JVMTI
  void adjust_default_methods(bool* trace_name_printed);
#endif // INCLUDE_JVMTI

  void clean_weak_instanceklass_links();
 private:
  void clean_implementors_list();
  void clean_method_data();

 public:
  // Explicit metaspace deallocation of fields
  // For RedefineClasses and class file parsing errors, we need to deallocate
  // instanceKlasses and the metadata they point to.
  void deallocate_contents(ClassLoaderData* loader_data);
  static void deallocate_methods(ClassLoaderData* loader_data,
                                 Array<Method*>* methods);
  void static deallocate_interfaces(ClassLoaderData* loader_data,
                                    const Klass* super_klass,
                                    Array<Klass*>* local_interfaces,
                                    Array<Klass*>* transitive_interfaces);

  // The constant pool is on stack if any of the methods are executing or
  // referenced by handles.
  bool on_stack() const { return _constants->on_stack(); }

  // callbacks for actions during class unloading
  static void notify_unload_class(InstanceKlass* ik);
  static void release_C_heap_structures(InstanceKlass* ik);

  // Naming
  const char* signature_name() const;
  static Symbol* package_from_name(const Symbol* name, TRAPS);

  // GC specific object visitors
  //
#if INCLUDE_PARALLELGC
  // Parallel Scavenge
  void oop_ps_push_contents(  oop obj, PSPromotionManager* pm);
  // Parallel Compact
  void oop_pc_follow_contents(oop obj, ParCompactionManager* cm);
  void oop_pc_update_pointers(oop obj, ParCompactionManager* cm);
#endif

  // Oop fields (and metadata) iterators
  //
  // The InstanceKlass iterators also visits the Object's klass.

  // Forward iteration
 public:
  // Iterate over all oop fields in the oop maps.
  template <typename T, class OopClosureType>
  inline void oop_oop_iterate_oop_maps(oop obj, OopClosureType* closure);

  // Iterate over all oop fields and metadata.
  template <typename T, class OopClosureType>
  inline int oop_oop_iterate(oop obj, OopClosureType* closure);

  // Iterate over all oop fields in one oop map.
  template <typename T, class OopClosureType>
  inline void oop_oop_iterate_oop_map(OopMapBlock* map, oop obj, OopClosureType* closure);


  // Reverse iteration
  // Iterate over all oop fields and metadata.
  template <typename T, class OopClosureType>
  inline int oop_oop_iterate_reverse(oop obj, OopClosureType* closure);

 private:
  // Iterate over all oop fields in the oop maps.
  template <typename T, class OopClosureType>
  inline void oop_oop_iterate_oop_maps_reverse(oop obj, OopClosureType* closure);

  // Iterate over all oop fields in one oop map.
  template <typename T, class OopClosureType>
  inline void oop_oop_iterate_oop_map_reverse(OopMapBlock* map, oop obj, OopClosureType* closure);


  // Bounded range iteration
 public:
  // Iterate over all oop fields in the oop maps.
  template <typename T, class OopClosureType>
  inline void oop_oop_iterate_oop_maps_bounded(oop obj, OopClosureType* closure, MemRegion mr);

  // Iterate over all oop fields and metadata.
  template <typename T, class OopClosureType>
  inline int oop_oop_iterate_bounded(oop obj, OopClosureType* closure, MemRegion mr);

 private:
  // Iterate over all oop fields in one oop map.
  template <typename T, class OopClosureType>
  inline void oop_oop_iterate_oop_map_bounded(OopMapBlock* map, oop obj, OopClosureType* closure, MemRegion mr);


 public:
  u2 idnum_allocated_count() const      { return _idnum_allocated_count; }

public:
  void set_in_error_state() {
    assert(DumpSharedSpaces, "only call this when dumping archive");
    _init_state = initialization_error;
  }
  bool check_sharing_error_state();

private:
  // initialization state
  void set_init_state(ClassState state);
  void set_rewritten()                  { _misc_flags |= _misc_rewritten; }
  void set_init_thread(Thread *thread)  { _init_thread = thread; }

  // The RedefineClasses() API can cause new method idnums to be needed
  // which will cause the caches to grow. Safety requires different
  // cache management logic if the caches can grow instead of just
  // going from NULL to non-NULL.
  bool idnum_can_increment() const      { return has_been_redefined(); }
  inline jmethodID* methods_jmethod_ids_acquire() const;
  inline void release_set_methods_jmethod_ids(jmethodID* jmeths);
  // This nulls out jmethodIDs for all methods in 'klass'
  static void clear_jmethod_ids(InstanceKlass* klass);

  // Lock during initialization
public:
  // Lock for (1) initialization; (2) access to the ConstantPool of this class.
  // Must be one per class and it has to be a VM internal object so java code
  // cannot lock it (like the mirror).
  // It has to be an object not a Mutex because it's held through java calls.
  oop init_lock() const;
private:
  void fence_and_clear_init_lock();

  bool link_class_impl                           (bool throw_verifyerror, TRAPS);
  bool verify_code                               (bool throw_verifyerror, TRAPS);
  void initialize_impl                           (TRAPS);
  void initialize_super_interfaces               (TRAPS);
  void eager_initialize_impl                     ();

  // Returns the array class for the n'th dimension
  Klass* array_klass_impl(bool or_null, int n, TRAPS);

  // Returns the array class with this class as element type
  Klass* array_klass_impl(bool or_null, TRAPS);

  // find a local method (returns NULL if not found)
  Method* find_method_impl(const Symbol* name,
                           const Symbol* signature,
                           OverpassLookupMode overpass_mode,
                           StaticLookupMode static_mode,
                           PrivateLookupMode private_mode) const;

  static Method* find_method_impl(const Array<Method*>* methods,
                                  const Symbol* name,
                                  const Symbol* signature,
                                  OverpassLookupMode overpass_mode,
                                  StaticLookupMode static_mode,
                                  PrivateLookupMode private_mode);

  // Free CHeap allocated fields.
  void release_C_heap_structures();

#if INCLUDE_JVMTI
  // RedefineClasses support
  void link_previous_versions(InstanceKlass* pv) { _previous_versions = pv; }
  void mark_newly_obsolete_methods(Array<Method*>* old_methods, int emcp_method_count);
#endif
public:
  // CDS support - remove and restore oops from metadata. Oops are not shared.
  virtual void remove_unshareable_info();
  virtual void remove_java_mirror();
  virtual void restore_unshareable_info(ClassLoaderData* loader_data, Handle protection_domain, TRAPS);

  // jvm support
  jint compute_modifier_flags(TRAPS) const;

public:
  // JVMTI support
  jint jvmti_class_status() const;

  virtual void metaspace_pointers_do(MetaspaceClosure* iter);

 public:
  // Printing
  void print_on(outputStream* st) const;
  void print_value_on(outputStream* st) const;

  void oop_print_value_on(oop obj, outputStream* st);

  void oop_print_on      (oop obj, outputStream* st);

#ifndef PRODUCT
  void print_dependent_nmethods(bool verbose = false);
  bool is_dependent_nmethod(nmethod* nm);
#endif

  const char* internal_name() const;

  // Verification
  void verify_on(outputStream* st);

  void oop_verify_on(oop obj, outputStream* st);

  // Logging
  void print_class_load_logging(ClassLoaderData* loader_data,
                                const char* module_name,
                                const ClassFileStream* cfs) const;
};

// for adding methods
// UNSET_IDNUM return means no more ids available
inline u2 InstanceKlass::next_method_idnum() {
  if (_idnum_allocated_count == ConstMethod::MAX_IDNUM) {
    return ConstMethod::UNSET_IDNUM; // no more ids available
  } else {
    return _idnum_allocated_count++;
  }
}


/* JNIid class for jfieldIDs only */
class JNIid: public CHeapObj<mtClass> {
  friend class VMStructs;
 private:
  Klass*             _holder;
  JNIid*             _next;
  int                _offset;
#ifdef ASSERT
  bool               _is_static_field_id;
#endif

 public:
  // Accessors
  Klass* holder() const           { return _holder; }
  int offset() const              { return _offset; }
  JNIid* next()                   { return _next; }
  // Constructor
  JNIid(Klass* holder, int offset, JNIid* next);
  // Identifier lookup
  JNIid* find(int offset);

  bool find_local_field(fieldDescriptor* fd) {
    return InstanceKlass::cast(holder())->find_local_field_from_offset(offset(), true, fd);
  }

  static void deallocate(JNIid* id);
  // Debugging
#ifdef ASSERT
  bool is_static_field_id() const { return _is_static_field_id; }
  void set_is_static_field_id()   { _is_static_field_id = true; }
#endif
  void verify(Klass* holder);
};

// An iterator that's used to access the inner classes indices in the
// InstanceKlass::_inner_classes array.
class InnerClassesIterator : public StackObj {
 private:
  Array<jushort>* _inner_classes;
  int _length;
  int _idx;
 public:

  InnerClassesIterator(const InstanceKlass* k) {
    _inner_classes = k->inner_classes();
    if (k->inner_classes() != NULL) {
      _length = _inner_classes->length();
      // The inner class array's length should be the multiple of
      // inner_class_next_offset if it only contains the InnerClasses
      // attribute data, or it should be
      // n*inner_class_next_offset+enclosing_method_attribute_size
      // if it also contains the EnclosingMethod data.
      assert((_length % InstanceKlass::inner_class_next_offset == 0 ||
              _length % InstanceKlass::inner_class_next_offset == InstanceKlass::enclosing_method_attribute_size),
             "just checking");
      // Remove the enclosing_method portion if exists.
      if (_length % InstanceKlass::inner_class_next_offset == InstanceKlass::enclosing_method_attribute_size) {
        _length -= InstanceKlass::enclosing_method_attribute_size;
      }
    } else {
      _length = 0;
    }
    _idx = 0;
  }

  int length() const {
    return _length;
  }

  void next() {
    _idx += InstanceKlass::inner_class_next_offset;
  }

  bool done() const {
    return (_idx >= _length);
  }

  u2 inner_class_info_index() const {
    return _inner_classes->at(
               _idx + InstanceKlass::inner_class_inner_class_info_offset);
  }

  void set_inner_class_info_index(u2 index) {
    _inner_classes->at_put(
               _idx + InstanceKlass::inner_class_inner_class_info_offset, index);
  }

  u2 outer_class_info_index() const {
    return _inner_classes->at(
               _idx + InstanceKlass::inner_class_outer_class_info_offset);
  }

  void set_outer_class_info_index(u2 index) {
    _inner_classes->at_put(
               _idx + InstanceKlass::inner_class_outer_class_info_offset, index);
  }

  u2 inner_name_index() const {
    return _inner_classes->at(
               _idx + InstanceKlass::inner_class_inner_name_offset);
  }

  void set_inner_name_index(u2 index) {
    _inner_classes->at_put(
               _idx + InstanceKlass::inner_class_inner_name_offset, index);
  }

  u2 inner_access_flags() const {
    return _inner_classes->at(
               _idx + InstanceKlass::inner_class_access_flags_offset);
  }
};

#endif // SHARE_VM_OOPS_INSTANCEKLASS_HPP
