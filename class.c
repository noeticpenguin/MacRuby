/* 
 * MacRuby implementation of Ruby 1.9's class.c.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 * 
 * Copyright (C) 2007-2008, Apple Inc. All rights reserved.
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 * Copyright (C) 2000 Network Applied Communication Laboratory, Inc.
 * Copyright (C) 2000 Information-technology Promotion Agency, Japan
 */

#include "ruby/ruby.h"
#include "ruby/signal.h"
#include "ruby/node.h"
#include "ruby/st.h"
#include "debug.h"
#include "id.h"
#include <ctype.h>

extern st_table *rb_class_tbl;

#define VISI(x) ((x)&NOEX_MASK)
#define VISI_CHECK(x,f) (VISI(x) == (f))

void rb_objc_install_array_primitives(Class);
void rb_objc_install_hash_primitives(Class);
void rb_objc_install_string_primitives(Class);

bool
rb_objc_install_primitives(Class ocklass, Class ocsuper)
{
    if (rb_cArray != 0 && rb_cHash != 0 && rb_cString != 0) {
	do {
	    if (ocsuper == (Class)rb_cArray) {
		rb_objc_install_array_primitives(ocklass);
		RCLASS_SET_VERSION_FLAG(ocklass, RCLASS_IS_ARRAY_SUBCLASS);
		return true;
	    }
	    if (ocsuper == (Class)rb_cHash) {
		rb_objc_install_hash_primitives(ocklass);
		RCLASS_SET_VERSION_FLAG(ocklass, RCLASS_IS_HASH_SUBCLASS);
		return true;
	    }
	    if (ocsuper == (Class)rb_cString) {
		rb_objc_install_string_primitives(ocklass);
		RCLASS_SET_VERSION_FLAG(ocklass, RCLASS_IS_STRING_SUBCLASS);
		return true;
	    }
	    ocsuper = class_getSuperclass(ocsuper);
	}
	while (ocsuper != NULL);
    }
    return false;
}

static VALUE
rb_class_allocate_instance(VALUE klass)
{
    NEWOBJ(obj, struct RObject);
    OBJSETUP(obj, klass, T_OBJECT);
    return (VALUE)obj;
}

static VALUE
rb_objc_alloc_class(const char *name, VALUE super, VALUE flags, VALUE klass)
{
    Class ocklass;
    char ocname[128];
    int version_flag;

    if (name == NULL) {
	static long anon_count = 1;
    	snprintf(ocname, sizeof ocname, "RBAnonymous%ld", ++anon_count);
    }
    else {
	if (objc_getClass(name) != NULL) {
	    long count = 1;
	    snprintf(ocname, sizeof ocname, "RB%s", name);
	    while (objc_getClass(ocname) != NULL)
		snprintf(ocname, sizeof ocname, "RB%s%ld", name, ++count);
	    rb_warning("can't create `%s' as an Objective-C class, because " \
		       "it already exists, instead using `%s'", name, ocname);
	}
	else {
	    strncpy(ocname, name, sizeof ocname);
	}
    }

    if (super == 0)
	super = rb_cObject;

    ocklass = objc_allocateClassPair((Class)super, ocname, sizeof(id));
    assert(ocklass != NULL);

    version_flag = RCLASS_IS_RUBY_CLASS;
    if (flags == T_MODULE) {
	version_flag |= RCLASS_IS_MODULE;
    }
    if (super == rb_cObject) {
	version_flag |= RCLASS_IS_OBJECT_SUBCLASS;
    }
    else if ((RCLASS_VERSION(super) & RCLASS_IS_OBJECT_SUBCLASS) == RCLASS_IS_OBJECT_SUBCLASS) {
	version_flag |= RCLASS_IS_OBJECT_SUBCLASS;
    }

    class_setVersion(ocklass, version_flag);

    DLOG("DEFC", "%s < %s (version=%d)", ocname, class_getName(class_getSuperclass((Class)ocklass)), version_flag);

    if (klass != 0)
	rb_objc_install_primitives(ocklass, (Class)super);

    return (VALUE)ocklass;
}

VALUE
rb_objc_create_class(const char *name, VALUE super)
{
    VALUE klass;
    
    klass = rb_objc_alloc_class(name, super, T_CLASS, rb_cClass);
    objc_registerClassPair((Class)klass);
   
    if (RCLASS_SUPER(klass) == rb_cNSObject) {
	rb_define_alloc_func(klass, rb_class_allocate_instance);
	rb_define_singleton_method(klass, "new", rb_class_new_instance, -1);
	rb_define_method(klass, "dup", rb_obj_dup, 0);
	rb_define_method(klass, "initialize_copy", rb_obj_init_copy, 1);
    }

    if (name != NULL && rb_class_tbl != NULL) 
	st_insert(rb_class_tbl, (st_data_t)rb_intern(name), (st_data_t)klass);

    return klass;
}

VALUE
rb_class_boot(VALUE super)
{
    VALUE klass = rb_objc_create_class(NULL, super);
    return (VALUE)klass;
}

void
rb_check_inheritable(VALUE super)
{
    if (TYPE(super) != T_CLASS) {
	rb_raise(rb_eTypeError, "superclass must be a Class (%s given)",
		 rb_obj_classname(super));
    }
    if (RCLASS_SINGLETON(super)) {
	rb_raise(rb_eTypeError, "can't make subclass of singleton class");
    }
}

VALUE
rb_class_new(VALUE super)
{
    Check_Type(super, T_CLASS);
    rb_check_inheritable(super);
    if (super == rb_cClass) {
	rb_raise(rb_eTypeError, "can't make subclass of Class");
    }
    return rb_class_boot(super);
}

/* :nodoc: */
VALUE
rb_mod_init_copy(VALUE clone, VALUE orig)
{
    rb_obj_init_copy(clone, orig);
    {
	VALUE super;
	int version_flag;

	if (orig == rb_cNSMutableString
	    || orig == rb_cNSMutableArray
	    || orig == rb_cNSMutableHash) {
	    super = orig;
	    rb_warn("cloning class `%s' is not supported, creating a " \
		    "subclass instead", rb_class2name(orig));
	}
	else {
	    super = RCLASS_SUPER(orig);
	}
	RCLASS_SUPER(clone) = super;

	version_flag = RCLASS_IS_RUBY_CLASS;
	if ((RCLASS_VERSION(super) & RCLASS_IS_OBJECT_SUBCLASS) == RCLASS_IS_OBJECT_SUBCLASS)
	    version_flag |= RCLASS_IS_OBJECT_SUBCLASS;

	class_setVersion((Class)clone, version_flag);
    }
#if 0 // TODO
    if (RCLASS_IV_TBL(orig)) {
	ID id;

	GC_WB(&RCLASS_IV_TBL(clone), st_copy(RCLASS_IV_TBL(orig)));
	id = rb_intern("__classpath__");
	st_delete(RCLASS_IV_TBL(clone), (st_data_t*)&id, 0);
	id = rb_intern("__classid__");
	st_delete(RCLASS_IV_TBL(clone), (st_data_t*)&id, 0);
    }
    if (RCLASS_M_TBL(orig)) {
	struct clone_method_data data;
	GC_WB(&RCLASS_M_TBL(clone), st_init_numtable());
	data.tbl = RCLASS_M_TBL(clone);
	data.klass = clone;
	st_foreach(RCLASS_M_TBL(orig), clone_method,
	  (st_data_t)&data);
    }
#endif

    return clone;
}

/* :nodoc: */
VALUE
rb_class_init_copy(VALUE clone, VALUE orig)
{
    if (RCLASS_SINGLETON(orig)) {
	rb_raise(rb_eTypeError, "can't copy singleton class");
    }
    clone =  rb_mod_init_copy(clone, orig);
    rb_objc_install_primitives((Class)clone, (Class)orig);
    return clone;
}

VALUE
rb_singleton_class_clone(VALUE obj)
{
    VALUE klass = RBASIC(obj)->klass;
    if (!RCLASS_SINGLETON(klass)) {
	return klass;
    }
    else {
	/* copy singleton(unnamed) class */
	VALUE clone = rb_objc_create_class(NULL, RCLASS_SUPER(klass));

	CFMutableDictionaryRef ivar_dict = rb_class_ivar_dict(klass);
	if (ivar_dict != NULL) {
	    CFMutableDictionaryRef cloned_ivar_dict;

	    cloned_ivar_dict = CFDictionaryCreateMutableCopy(NULL, 0, (CFDictionaryRef)ivar_dict);
	    rb_class_ivar_set_dict(clone, cloned_ivar_dict);
	    CFMakeCollectable(cloned_ivar_dict);
	}

	Method *methods;
	unsigned i, methods_count;
	methods = class_copyMethodList((Class)klass, &methods_count);
	if (methods != NULL) {
	    for (i = 0; i < methods_count; i++) {
		Method method = methods[i], method2;
		method2 = class_getInstanceMethod((Class)clone, method_getName(method));
		if (method2 != class_getInstanceMethod((Class)RCLASS_SUPER(clone), method_getName(method))) {
		    method_setImplementation(method2, method_getImplementation(method));
		}
		else {
		    assert(class_addMethod((Class)clone, 
				method_getName(method), 
				method_getImplementation(method), 
				method_getTypeEncoding(method)));
		}
	    }
	    free(methods);
	}

	rb_singleton_class_attached(RBASIC(clone)->klass, (VALUE)clone);
	if (RCLASS_SUPER(clone) == rb_cNSObject) {
	    RCLASS_VERSION(clone) ^= RCLASS_IS_OBJECT_SUBCLASS;
	}
	RCLASS_SET_VERSION_FLAG(clone, RCLASS_IS_SINGLETON);

	return clone;
    }
}

void
rb_singleton_class_attached(VALUE klass, VALUE obj)
{
    if (RCLASS_SINGLETON(klass)) {
	static ID attachedId = 0;
	if (attachedId == 0)
	    attachedId = rb_intern("__attached__");
	rb_ivar_set(klass, attachedId, obj);
    }
}

VALUE
rb_make_metaclass(VALUE obj, VALUE super)
{
    if (TYPE(obj) == T_CLASS && RCLASS_SINGLETON(obj)) {
	RBASIC(obj)->klass = rb_cClass;
	return rb_cClass;
    }
    else {
	VALUE klass;

	klass = rb_class_boot(super);
	RBASIC(obj)->klass = klass;
	if (super == rb_cNSObject) {
	    RCLASS_VERSION(klass) ^= RCLASS_IS_OBJECT_SUBCLASS;
	}
	RCLASS_SET_VERSION_FLAG(klass, RCLASS_IS_SINGLETON);

	rb_singleton_class_attached(klass, obj);

	return klass;
    }
}

VALUE
rb_define_class_id(ID id, VALUE super)
{
    VALUE klass;

    if (!super) super = rb_cObject;
    klass = rb_objc_create_class(rb_id2name(id), super);

    return klass;
}

VALUE
rb_class_inherited(VALUE super, VALUE klass)
{
    if (!super) super = rb_cObject;
    return rb_funcall(super, rb_intern("inherited"), 1, klass);
}

VALUE
rb_define_class(const char *name, VALUE super)
{
    VALUE klass;
    ID id;

    id = rb_intern(name);
    if (rb_const_defined(rb_cObject, id)) {
	klass = rb_const_get(rb_cObject, id);
	if (TYPE(klass) != T_CLASS) {
	    rb_raise(rb_eTypeError, "%s is not a class", name);
	}
	if (rb_class_real(RCLASS_SUPER(klass)) != super) {
	    rb_name_error(id, "%s is already defined", name);
	}
	return klass;
    }
    if (!super) {
	rb_warn("no super class for `%s', Object assumed", name);
    }
    klass = rb_define_class_id(id, super);
    st_add_direct(rb_class_tbl, id, klass);
    rb_name_class(klass, id);
    rb_const_set(rb_cObject, id, klass);
    rb_class_inherited(super, klass);

    return klass;
}

VALUE
rb_define_class_under(VALUE outer, const char *name, VALUE super)
{
    VALUE klass;
    ID id;

    id = rb_intern(name);
    if (rb_const_defined_at(outer, id)) {
	klass = rb_const_get_at(outer, id);
	if (TYPE(klass) != T_CLASS) {
	    rb_raise(rb_eTypeError, "%s is not a class", name);
	}
	if (rb_class_real(RCLASS_SUPER(klass)) != super) {
	    rb_name_error(id, "%s is already defined", name);
	}
	return klass;
    }
    if (!super) {
	rb_warn("no super class for `%s::%s', Object assumed",
		rb_class2name(outer), name);
    }
    klass = rb_define_class_id(id, super);
    rb_set_class_path(klass, outer, name);
    rb_const_set(outer, id, klass);
    rb_class_inherited(super, klass);

    return klass;
}

VALUE
rb_module_new(void)
{
    VALUE mdl = rb_objc_alloc_class(NULL, 0, T_MODULE, rb_cModule);
    objc_registerClassPair((Class)mdl);

    return (VALUE)mdl;
}

VALUE
rb_define_module_id(ID id)
{
    VALUE mdl;

    mdl = rb_objc_alloc_class(rb_id2name(id), 0, T_MODULE, rb_cModule);
    objc_registerClassPair((Class)mdl);

    return mdl;
}

VALUE
rb_define_module(const char *name)
{
    VALUE module;
    ID id;

    id = rb_intern(name);
    if (rb_const_defined(rb_cObject, id)) {
	module = rb_const_get(rb_cObject, id);
	if (TYPE(module) == T_MODULE)
	    return module;
	rb_raise(rb_eTypeError, "%s is not a module", rb_obj_classname(module));
    }
    module = rb_define_module_id(id);
    st_add_direct(rb_class_tbl, id, module);
    rb_const_set(rb_cObject, id, module);

    return module;
}

VALUE
rb_define_module_under(VALUE outer, const char *name)
{
    VALUE module;
    ID id;

    id = rb_intern(name);
    if (rb_const_defined_at(outer, id)) {
	module = rb_const_get_at(outer, id);
	if (TYPE(module) == T_MODULE)
	    return module;
	rb_raise(rb_eTypeError, "%s::%s is not a module",
		 rb_class2name(outer), rb_obj_classname(module));
    }
    module = rb_define_module_id(id);
    rb_const_set(outer, id, module);
    rb_set_class_path(module, outer, name);

    return module;
}

void
rb_include_module(VALUE klass, VALUE module)
{
    Method *methods;
    unsigned int i, methods_count;
    VALUE ary;

    rb_frozen_class_p(klass);

    if (!OBJ_TAINTED(klass))
	rb_secure(4);

    Check_Type(module, T_MODULE);

    ary = rb_ivar_get(klass, idIncludedModules);
    if (ary == Qnil) {
	ary = rb_ary_new();
	rb_ivar_set(klass, idIncludedModules, ary);
    }
    if (rb_ary_includes(ary, module))
	return;
    rb_ary_insert(ary, 0, module);

    ary = rb_ivar_get(module, idIncludedInClasses);
    if (ary == Qnil) {
	ary = rb_ary_new();
	rb_ivar_set(module, idIncludedInClasses, ary);
    }
    rb_ary_push(ary, klass);

    DLOG("INCM", "%s <- %s", class_getName((Class)klass), class_getName((Class)module));

    methods = class_copyMethodList((Class)module, &methods_count);
    if (methods != NULL) {
	for (i = 0; i < methods_count; i++) {
	    Method method = methods[i], method2;
	    DLOG("DEFI", "-[%s %s]", class_getName((Class)klass), (char *)method_getName(method));
	
	    method2 = class_getInstanceMethod((Class)klass, method_getName(method));
	    if (method2 != NULL && method2 != class_getInstanceMethod((Class)RCLASS_SUPER(klass), method_getName(method))) {
		method_setImplementation(method2, method_getImplementation(method));
	    }
	    else {
		assert(class_addMethod((Class)klass, 
			    method_getName(method), 
			    method_getImplementation(method), 
			    method_getTypeEncoding(method)));
	    }
	}
	free(methods);
    }
}

/*
 *  call-seq:
 *     mod.included_modules -> array
 *  
 *  Returns the list of modules included in <i>mod</i>.
 *     
 *     module Mixin
 *     end
 *     
 *     module Outer
 *       include Mixin
 *     end
 *     
 *     Mixin.included_modules   #=> []
 *     Outer.included_modules   #=> [Mixin]
 */

static void
rb_mod_included_modules_nosuper(VALUE mod, VALUE ary)
{
    VALUE inc_mods = rb_ivar_get(mod, idIncludedModules);
    if (inc_mods != Qnil) {
	int i, count = RARRAY_LEN(inc_mods);
	for (i = 0; i < count; i++) {
	    VALUE imod = RARRAY_AT(inc_mods, i);
	    rb_ary_push(ary, imod);
	    rb_ary_concat(ary, rb_mod_included_modules(imod));
	}
    }
}

VALUE
rb_mod_included_modules(VALUE mod)
{
    VALUE p, ary = rb_ary_new();

    for (p = mod; p; p = RCLASS_SUPER(p)) {
	rb_mod_included_modules_nosuper(p, ary);
	if (RCLASS_MODULE(p))
	    break;
    }
    return ary;
}

/*
 *  call-seq:
 *     mod.include?(module)    => true or false
 *  
 *  Returns <code>true</code> if <i>module</i> is included in
 *  <i>mod</i> or one of <i>mod</i>'s ancestors.
 *     
 *     module A
 *     end
 *     class B
 *       include A
 *     end
 *     class C < B
 *     end
 *     B.include?(A)   #=> true
 *     C.include?(A)   #=> true
 *     A.include?(A)   #=> false
 */

VALUE
rb_mod_include_p(VALUE mod, VALUE mod2)
{
    return rb_ary_includes(rb_mod_included_modules(mod), mod2);
}

/*
 *  call-seq:
 *     mod.ancestors -> array
 *  
 *  Returns a list of modules included in <i>mod</i> (including
 *  <i>mod</i> itself).
 *     
 *     module Mod
 *       include Math
 *       include Comparable
 *     end
 *     
 *     Mod.ancestors    #=> [Mod, Comparable, Math]
 *     Math.ancestors   #=> [Math]
 */

static void rb_mod_included_modules_nosuper(VALUE, VALUE);

VALUE
rb_mod_ancestors(VALUE mod)
{
    VALUE p, ary = rb_ary_new();
   
    for (p = mod; p; p = RCLASS_SUPER(p)) {
	rb_ary_push(ary, p);
	rb_mod_included_modules_nosuper(p, ary);
	if (RCLASS_MODULE(p))
	    break;
    }
    return ary;
}

static int
ins_methods_push(ID name, long type, VALUE ary, long visi)
{
    if (type == -1) return ST_CONTINUE;

    switch (visi) {
      case NOEX_PRIVATE:
      case NOEX_PROTECTED:
      case NOEX_PUBLIC:
	visi = (type == visi);
	break;
      default:
	visi = (type != NOEX_PRIVATE);
	break;
    }
    if (visi) {
	rb_ary_push(ary, ID2SYM(name));
    }
    return ST_CONTINUE;
}

static int
ins_methods_i(ID name, long type, VALUE ary)
{
    return ins_methods_push(name, type, ary, -1); /* everything but private */
}

static int
ins_methods_prot_i(ID name, long type, VALUE ary)
{
    return ins_methods_push(name, type, ary, NOEX_PROTECTED);
}

static int
ins_methods_priv_i(ID name, long type, VALUE ary)
{
    return ins_methods_push(name, type, ary, NOEX_PRIVATE);
}

static int
ins_methods_pub_i(ID name, long type, VALUE ary)
{
    return ins_methods_push(name, type, ary, NOEX_PUBLIC);
}

static void
rb_objc_push_methods(VALUE ary, VALUE mod)
{
    Method *methods;
    unsigned int i, count;

    methods = class_copyMethodList((Class)mod, &count); 
    if (methods != NULL) {  
	for (i = 0; i < count; i++) { 
	    Method method;
	    SEL sel;
	    char *sel_name, *p;
	    VALUE sym;
	    ID mid;

	    method = methods[i];

	    sel = method_getName(method);
	    if (sel == sel_ignored)
		continue; 

	    sel_name = (char *)sel;

	    if (rb_objc_method_node3(method_getImplementation(method)) == NULL
		&& *sel_name == '_')
		continue;

	    p = strchr(sel_name, ':');
	    if (p != NULL && strchr(p + 1, ':') == NULL) {
		size_t len = strlen(sel_name);
		char buf[100];

		assert(len < sizeof(buf));

		if (len > 4 && sel_name[0] == 's' && sel_name[1] == 'e' 
		    && sel_name[2] == 't' && isupper(sel_name[3])) {
		    snprintf(buf, sizeof buf, "%s", &sel_name[3]);
		    buf[len - 4] = '=';
		    buf[0] = tolower(buf[0]);
		}
		else {
		    strncpy(buf, sel_name, len);
		    buf[len - 1] = '\0';
		}

		mid = rb_intern(buf);
	    }
	    else {
		mid = rb_intern(sel_name);
	    }

	    sym = ID2SYM(mid);

	    if (rb_ary_includes(ary, sym) == Qfalse)
		rb_ary_push(ary, sym);
	} 
	free(methods); 
    }
}

static VALUE
class_instance_method_list(int argc, VALUE *argv, VALUE mod, int (*func) (ID, long, VALUE))
{
    VALUE ary;
    bool recur;

    ary = rb_ary_new();

    if (argc == 0) {
	recur = true;
    }
    else {
	VALUE r;
	rb_scan_args(argc, argv, "01", &r);
	recur = RTEST(r);
    }

    while (mod != 0) {
	rb_objc_push_methods(ary, mod);
	if (!recur)
	   break;	   
	mod = (VALUE)class_getSuperclass((Class)mod); 
    } 

    return ary;
}

/*
 *  call-seq:
 *     mod.instance_methods(include_super=true)   => array
 *  
 *  Returns an array containing the names of public instance methods in
 *  the receiver. For a module, these are the public methods; for a
 *  class, they are the instance (not singleton) methods. With no
 *  argument, or with an argument that is <code>false</code>, the
 *  instance methods in <i>mod</i> are returned, otherwise the methods
 *  in <i>mod</i> and <i>mod</i>'s superclasses are returned.
 *     
 *     module A
 *       def method1()  end
 *     end
 *     class B
 *       def method2()  end
 *     end
 *     class C < B
 *       def method3()  end
 *     end
 *     
 *     A.instance_methods                #=> [:method1]
 *     B.instance_methods(false)         #=> [:method2]
 *     C.instance_methods(false)         #=> [:method3]
 *     C.instance_methods(true).length   #=> 43
 */

VALUE
rb_class_instance_methods(int argc, VALUE *argv, VALUE mod)
{
    return class_instance_method_list(argc, argv, mod, ins_methods_i);
}

/*
 *  call-seq:
 *     mod.protected_instance_methods(include_super=true)   => array
 *  
 *  Returns a list of the protected instance methods defined in
 *  <i>mod</i>. If the optional parameter is not <code>false</code>, the
 *  methods of any ancestors are included.
 */

VALUE
rb_class_protected_instance_methods(int argc, VALUE *argv, VALUE mod)
{
    return class_instance_method_list(argc, argv, mod, ins_methods_prot_i);
}

/*
 *  call-seq:
 *     mod.private_instance_methods(include_super=true)    => array
 *  
 *  Returns a list of the private instance methods defined in
 *  <i>mod</i>. If the optional parameter is not <code>false</code>, the
 *  methods of any ancestors are included.
 *     
 *     module Mod
 *       def method1()  end
 *       private :method1
 *       def method2()  end
 *     end
 *     Mod.instance_methods           #=> [:method2]
 *     Mod.private_instance_methods   #=> [:method1]
 */

VALUE
rb_class_private_instance_methods(int argc, VALUE *argv, VALUE mod)
{
    return class_instance_method_list(argc, argv, mod, ins_methods_priv_i);
}

/*
 *  call-seq:
 *     mod.public_instance_methods(include_super=true)   => array
 *  
 *  Returns a list of the public instance methods defined in <i>mod</i>.
 *  If the optional parameter is not <code>false</code>, the methods of
 *  any ancestors are included.
 */

VALUE
rb_class_public_instance_methods(int argc, VALUE *argv, VALUE mod)
{
    return class_instance_method_list(argc, argv, mod, ins_methods_pub_i);
}

/*
 *  call-seq:
 *     obj.singleton_methods(all=true)    => array
 *  
 *  Returns an array of the names of singleton methods for <i>obj</i>.
 *  If the optional <i>all</i> parameter is true, the list will include
 *  methods in modules included in <i>obj</i>.
 *     
 *     module Other
 *       def three() end
 *     end
 *     
 *     class Single
 *       def Single.four() end
 *     end
 *     
 *     a = Single.new
 *     
 *     def a.one()
 *     end
 *     
 *     class << a
 *       include Other
 *       def two()
 *       end
 *     end
 *     
 *     Single.singleton_methods    #=> [:four]
 *     a.singleton_methods(false)  #=> [:two, :one]
 *     a.singleton_methods         #=> [:two, :one, :three]
 */

VALUE
rb_obj_singleton_methods(int argc, VALUE *argv, VALUE obj)
{
    VALUE recur, klass, ary;

    if (argc == 0) {
	recur = Qtrue;
    }
    else {
	rb_scan_args(argc, argv, "01", &recur);
    }

    klass = CLASS_OF(obj);
    ary = rb_ary_new();

    do {
	if (RCLASS_SINGLETON(klass))
	    rb_objc_push_methods(ary, klass);
	klass = RCLASS_SUPER(klass);
    }
    while (recur == Qtrue && klass != 0);

    return ary;
}

void
rb_define_method_id(VALUE klass, ID name, VALUE (*func)(ANYARGS), int argc)
{
    rb_add_method(klass, name, NEW_CFUNC(func,argc), NOEX_PUBLIC);
}

void
rb_define_method(VALUE klass, const char *name, VALUE (*func)(ANYARGS), int argc)
{
    rb_add_method(klass, rb_intern(name), NEW_CFUNC(func, argc), NOEX_PUBLIC);
}

void
rb_define_protected_method(VALUE klass, const char *name, VALUE (*func)(ANYARGS), int argc)
{
    rb_add_method(klass, rb_intern(name), NEW_CFUNC(func, argc), NOEX_PROTECTED);
}

void
rb_define_private_method(VALUE klass, const char *name, VALUE (*func)(ANYARGS), int argc)
{
    rb_add_method(klass, rb_intern(name), NEW_CFUNC(func, argc), NOEX_PRIVATE);
}

void
rb_undef_method(VALUE klass, const char *name)
{
    rb_add_method(klass, rb_intern(name), 0, NOEX_UNDEF);
}

#define SPECIAL_SINGLETON(x,c) do {\
    if (obj == (x)) {\
	return c;\
    }\
} while (0)

VALUE
rb_singleton_class(VALUE obj)
{
    VALUE klass;

    if (FIXNUM_P(obj) || SYMBOL_P(obj)) {
	rb_raise(rb_eTypeError, "can't define singleton");
    }
    if (rb_special_const_p(obj)) {
	SPECIAL_SINGLETON(Qnil, rb_cNilClass);
	SPECIAL_SINGLETON(Qfalse, rb_cFalseClass);
	SPECIAL_SINGLETON(Qtrue, rb_cTrueClass);
	rb_bug("unknown immediate %ld", obj);
    }

    DEFER_INTS;
    if (RCLASS_SINGLETON(RBASIC(obj)->klass) &&
	rb_iv_get(RBASIC(obj)->klass, "__attached__") == obj) {
	klass = RBASIC(obj)->klass;
    }
    else {
	switch (TYPE(obj)) {
	    case T_CLASS:
	    case T_MODULE:
		klass = *(VALUE *)obj;
		break;
	    default:
		klass = rb_make_metaclass(obj, RBASIC(obj)->klass);
		break;
	}
    }
#if 0
    if (OBJ_TAINTED(obj)) {
	OBJ_TAINT(klass);
    }
    else {
	OBJ_UNTAINT(klass);
    }
#endif
    if (OBJ_FROZEN(obj)) OBJ_FREEZE(klass);
    ALLOW_INTS;

    return klass;
}

void
rb_define_singleton_method(VALUE obj, const char *name, VALUE (*func)(ANYARGS), int argc)
{
    rb_define_method(rb_singleton_class(obj), name, func, argc);
}

void
rb_define_module_function(VALUE module, const char *name, VALUE (*func)(ANYARGS), int argc)
{
    rb_define_private_method(module, name, func, argc);
    rb_define_singleton_method(module, name, func, argc);
}

void
rb_define_global_function(const char *name, VALUE (*func)(ANYARGS), int argc)
{
    rb_define_module_function(rb_mKernel, name, func, argc);
}

void
rb_define_alias(VALUE klass, const char *name1, const char *name2)
{
    rb_alias(klass, rb_intern(name1), rb_intern(name2));
}

void
rb_define_attr(VALUE klass, const char *name, int read, int write)
{
    rb_attr(klass, rb_intern(name), read, write, Qfalse);
}

#include <stdarg.h>

int
rb_scan_args(int argc, const VALUE *argv, const char *fmt, ...)
{
    int n, i = 0;
    const char *p = fmt;
    VALUE *var;
    va_list vargs;

    va_start(vargs, fmt);

    if (*p == '*') goto rest_arg;

    if (ISDIGIT(*p)) {
	n = *p - '0';
	if (n > argc)
	    rb_raise(rb_eArgError, "wrong number of arguments (%d for %d)", argc, n);
	for (i=0; i<n; i++) {
	    var = va_arg(vargs, VALUE*);
	    if (var) *var = argv[i];
	}
	p++;
    }
    else {
	goto error;
    }

    if (ISDIGIT(*p)) {
	n = i + *p - '0';
	for (; i<n; i++) {
	    var = va_arg(vargs, VALUE*);
	    if (argc > i) {
		if (var) *var = argv[i];
	    }
	    else {
		if (var) *var = Qnil;
	    }
	}
	p++;
    }

    if (*p == '*') {
      rest_arg:
	var = va_arg(vargs, VALUE*);
	if (argc > i) {
	    if (var) *var = rb_ary_new4(argc-i, argv+i);
	    i = argc;
	}
	else {
	    if (var) *var = rb_ary_new();
	}
	p++;
    }

    if (*p == '&') {
	var = va_arg(vargs, VALUE*);
	if (rb_block_given_p()) {
	    *var = rb_block_proc();
	}
	else {
	    *var = Qnil;
	}
	p++;
    }
    va_end(vargs);

    if (*p != '\0') {
	goto error;
    }

    if (argc > i) {
	rb_raise(rb_eArgError, "wrong number of arguments (%d for %d)", argc, i);
    }

    return argc;

  error:
    rb_fatal("bad scan arg format: %s", fmt);
    return 0;
}
