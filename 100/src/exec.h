#ifndef __EXEC_H__
#define __EXEC_H__ 1
/*
 * A compiled program consists of several data blocks, all allocated
 * contiguos in memory to enhance the working set. At the compilation,
 * the blocks will be allocated separately, as the final size is
 * unknow. When compilation is done, the blocks will be copied into
 * the one big area.
 *
 * There are 8 different blocks of information for each program:
 * 1. The program itself. Consists of machine code instructions for a virtual
 *    stack machine. The size of the program must not be bigger than
 *    65535 bytes, as 16 bit pointers are used. Who would ever need a bigger
 *    program :-)
 * 2. Function names. All local functions that has been defined or called,
 *    with the address of the function in the program. Inherited functions
 *    will be found here too, with information of how far up the inherit
 *    chain that the function was defined.
 * 3. String table. All strings used in the program. They are all pointers
 *    into the shared string area. Thus, they are easily found and deallocated
 *    when the object is destructed.
 * 4. Table of variable names. They all point into the shared string table.
 * 5. Line number information. A table which tells at what address every
 *    line belongs to. The table has the same number of entries as the
 *    programs has source lines. This is used at errors, to find out the
 *    line number of the error.
 * 6. List of inherited objects.
* Program code is made up of series of functions.
 * Every function consists of a header followed by the bytecode:
   char *name : shared string (4 bytes)
   return type          (1)
-->num_arg              (1): Bit 7: ???, Bits 6..0: Number of expected args.
   num_local            (1)
   executable code...
 * --> marks the address given by program->functions[] & FUNSTART_MASK
 * TODO: This should be done in a struct, with nice macros for the
 * TODO:: address mangling. But be aware that a struct might introduce padding.
 *
 * Notes:
 * - Instructions with values > 255 are split in two bytes. The Highbyte
 *   is (inst / 128), the low byte inst % 128. The resulting values
 *   for the highbyte are 'coincidentally' the values of F_ESCAPE, F_TEFUN
 *   and F_VEFUN. The divisor 128 is symbolically defined by F_ESCAPE_BITS
 *   below.
 * - Imagine: A and B inherit nothing, C inherits A, D inherits C and B.
 *   Then the function and variable blocks are set up like this ('-funs' are
 *   real function entries, '-desc' are pointers to inherit descriptors):
 *     A-fblock: A-funs  (B similar)
 *     A-vblock: A-vars  (B similar)
 *     
 *     C-fblock: C-funs A-desc
 *     C-vblock: C-vars A-vars
 *
 *     D-fblock: D-funs (C-desc A-desc) B-desc
 *     D-vblock: D-vars  C-vars A-vars  B-vars
 *       and fblock has the twist that (C-desc A-desc) are together considered
 *       being 'the' C-desc block.
 * - Strings of all kind used by a program are stored as shared strings in an
 *   array and referenced from the program code by index.
 *   TODO: DOes the compiler try to minimize the array, by storing each string
 *   TODO:: ref only once?
 * - The code is geared towards 8-Bit-Chars for the bytecode (duh! :-),
 *   but not always uses EXTRACT_UCHAR() to decompose it. Bad for machines
 *   where a char is bigger.
 * - Branches are relative to the first byte of the
 *   following instruction. 
 */

#include "driver.h"
#include "datatypes.h" /* struct svalue */

/*
 * When an new object inherits from another, all function definitions
 * are copied, and all variable definitions.
 * Flags with NAME_ can't explicitly declared. Flags that can be declared,
 * are found with TYPE_ below.
 *
 * When an object is compiled with type testing (#pragma strict_types), all
 * types are saved of the arguments for that function during compilation.
 * If the #pragma save_types is specified, then the types are saved even
 * after compilation, to be used when the object is inherited.
 */

/* --- Byte code ---
 *
 * The program code is stored as byte code. The following definitions
 * and macros allow its implementation even on platforms with more than
 * 8 bits per character.
 * TODO: This portability is far from complete, and not used everywhere.
 *
 *   bytecode_t: an integral type holding the numbers 0..255.
 *   bytecode_p: an integral type addressing a bytecode. This need not
 *               be a pointer.
 *               
 *   bytecode_t GET_CODE(bytecode_p p)
 *   bytecode_t LOAD_CODE(bytecode_p p)
 *     Return the bytecode from *p, the LOAD_ variant then increments p.
 *     
 *    char GET_INT8(p) , LOAD_INT8(p)
 *   uchar GET_UINT8(p), LOAD_UINT8(p)
 *     Return the 8-Bit (unsigned) int stored at <p>, the LOAD_ variants
 *     then increment <p>.
 *     
 *   void GET_SHORT ([unsigned] short d, bytecode_p p)
 *   void LOAD_SHORT([unsigned] short d, bytecode_p p)
 *     Load the (unsigned) short 'd' stored at <p>, the LOAD_ variant
 *     then increments <p>.
 *
 *   void GET_INT16 ([unsigned] int16 d, bytecode_p p)
 *   void LOAD_INT16([unsigned] int16 d, bytecode_p p)
 *     Load the (unsigned) int16 'd' stored at <p>, the LOAD_ variant
 *     then increments <p>.
 *
 *   void LOAD_INT32([unsigned] int32 d, bytecode_p p)
 *     Load the (unsigned) in32 'd' stored at <p>, then increment <p>.
 */

#define F_ESCAPE_BITS 7

#if CHAR_BIT == 8
typedef unsigned char   bytecode_t;
typedef bytecode_t    * bytecode_p;

#define GET_CODE(p)    (*(p))
#define LOAD_CODE(p)   (*(p)++)

#define GET_UINT8(p)   (*((unsigned char *)(p)))
#define GET_INT8(p)    (*((signed char *)(p)))
#define LOAD_UINT8(p)   (*((unsigned char *)(p)++))
#define LOAD_INT8(p)    (*((signed char *)(p)++))

/* TODO: These generic mem-macros should go into a macros.h. See also
 * TODO:: how mudos does it.
 */
#define LOAD_2BYTE(d,p) ( ((char *)&(d))[0] = *(char *)(p)++, \
                          ((char *)&(d))[1] = *(char *)(p)++)

#define GET_2BYTE(d,p)  ( ((char *)&(d))[0] = ((char *)(p))[0], \
                          ((char *)&(d))[1] = ((char *)(p))[1] )

#define LOAD_4BYTE(d,p) ( ((char *)&(d))[0] = *(char *)(p)++, \
                          ((char *)&(d))[1] = *(char *)(p)++, \
                          ((char *)&(d))[2] = *(char *)(p)++, \
                          ((char *)&(d))[3] = *(char *)(p)++ )
    
#define GET_SHORT(d,p)  GET_2BYTE(d,p)
#define LOAD_SHORT(d,p) LOAD_2BYTE(d,p)
  /* TODO: This assumes sizeof(short) == 2 */

#define LOAD_INT16(d,p) LOAD_2BYTE(d,p)
#define LOAD_INT32(d,p) LOAD_2BYTE(d,p)

#endif

#ifndef GET_CODE
#  error "No bytecode type defined."
#endif

/* --- Function flags ---
 *
 * Programs know about their functions from an array of uint32s which hold
 * the flags for the function and, in one field in the low bits, the
 * address of the code.
 *
 * Some flags (TYPE_MOD_VIRTUAL) are also used for variable flags.
 *
 * Using a bitfield is tempting, but generates inefficient code due to the
 * lack of a real 'bool' datatype.
 * 
 * TODO: Function flags should be its own type 'funflag', not just 'uint32'.
 */

#define NAME_INHERITED      0x80000000  /* defined by inheritance */
#define TYPE_MOD_STATIC     0x40000000  /* Static function or variable    */
#define TYPE_MOD_NO_MASK    0x20000000  /* The nomask => not redefineable */
#define TYPE_MOD_PRIVATE    0x10000000  /* Can't be inherited             */
#define TYPE_MOD_PUBLIC     0x08000000  /* Force inherit through private  */
#define TYPE_MOD_VARARGS    0x04000000  /* Used for type checking         */
#define NAME_INITIALIZED    0x04000000  /* only used for variables        */
#define TYPE_MOD_VIRTUAL    0x02000000  /* can be re- and cross- defined  */
#define TYPE_MOD_PROTECTED  0x01000000  /* cannot be called externally    */
#define TYPE_MOD_XVARARGS   0x00800000  /* accepts optional arguments     */

#define FUNSTART_MASK       0x000fffff
  /* Function not inherited: unsigned address of the function code relative
   * to the begin of the program block (struct program->program).
   */

#define NAME_CROSS_DEFINED  0x00080000
  /* Function from A used in B, resolved by C which inherits both A and B.
   * If this is true, the value (flags & INHERIT_MASK) (in bias-0x800000
   * representation) is the difference between the current function index
   * and the real functions index.
   */
 
#define INHERIT_MASK        0x0003ffff
  /* Function inherited: unsigned index of the parent program descriptor
   * in the struct program->inherit[] array. In the parent program, this
   * function may again be inherited.
   */


#define NAME_HIDDEN         0x00000800 /* Not visible for inheritance    */
#define NAME_PROTOTYPE      0x00000400 /* Defined by a prototype only    */
#define NAME_UNDEFINED      0x00000200 /* Not defined yet                */
#define NAME_TYPES_LOST     0x00000100 /* inherited, no save_types       */

/* --- Function header ---
 *
 * The bytecode for every function is preceeded by a header with the name
 * and information about arguments and types:
 *
 * struct fun_hdr {
 *     shared char * name_of_function; (4 Bytes)
 *     byte          return_type;        (1 Byte)
 * --> byte          number_formal_args; (1 Byte)
 *         Bit 7: set if the function has a 'varargs' argument
 * TODO: some code makes use of the fact that this makes the number negative
 *         Bit 6..0: the number of arguments
 *     byte          number_local_vars;  (1 Byte)
 *         This includes the svalues needed for the break stack for
 *         switch() statements.
 *     bytecode_t    opcode[...]
 * }
 *
 * The function address given in the program's function block points to
 * .number_formal_args.
 *
 * Since structs introduce uncontrollable padding, access of all fields
 * is implemented using macros taking the 'function address', typedef'd
 * as fun_hdr_p, as argument and evaluate to the desired value. 
 *
 * Note: Changes here can affect the struct lambda layout and associated
 *       constants.
 * TODO: the other fields should have proper types, too.
 * TODO: the whole information should be in a table, and not in the
 * TODO:: bytecode. See struct program.
 */

typedef bytecode_p fun_hdr_p;
  /* TODO: Make this a void* for now? */

#define FUNCTION_NAME(p)      (*((char **)((char *)p - sizeof(char) - sizeof(char *))))
#define FUNCTION_TYPE(p)      (*((unsigned char *)((char *)p - sizeof(char))))
#define FUNCTION_NUM_ARGS(p)  EXTRACT_SCHAR((char *)p)
#define FUNCTION_NUM_VARS(p)  (*((unsigned char *)((char *)p + sizeof(char))))
#define FUNCTION_CODE(p)      ((bytecode_p)((unsigned char *)p + 2* sizeof(char)))

/* --- struct function: TODO: ???
 */

struct function /* TODO: used by compiler and simul_efun only? */
{
    char *name;  /* TODO: Name of function (shared?) */
      /* This is needed very often, therefore it should be first (allocated) */
    union {
        uint32 pc;       /* TODO: Address of function */
        uint32 inherit;  /* TODO: inherit table index when inherited. */
        /*signed*/ int32 func;  /* offset to first inherited function
                                 * with this name.
                                 * simul_efun also uses this field as a next
                                 * index in the simul_efun function table for
                                 * functions that have been discarded due to a
                                 * change in the number of arguments.
                                 */
        struct function *next;        /* used for mergesort */
    } offset;
    /* TODO: funflag */ uint32 flags;
    unsigned short type;      /* Return type of function. See below. */
    unsigned char num_local;  /* Number of local variables */
    unsigned char num_arg;    /* Number of arguments needed. */
#   define SIMUL_EFUN_VARARGS  0xff  /* Magic num_arg value for varargs */
};

/* --- struct variable: description of one variable
 *
 * This structure describes one variable, inherited or own.
 * The type part of the .flags is used just by the compiler.
 */

struct variable
{
    char   *name;   /* Name of the variable (TODO: shared?) */
    uint32  flags;
      /* Flags and type of the variable.
       * If a variable is inherited virtually, the function flag
       * TYPE_MOD_VIRTUAL is or'ed on this.
       */
};

/* --- struct inherit: description of one inherited program
 *
 * The information about inherited programs ("objects") for a given
 * program is stored in an array of these structure, and the inherited
 * programs are accessed from the childs' program code by indexing this array.
 *
 * If a program is inherited virtually several times, each virtual inherit
 * gets its own struct inherit, together with reserved space in the
 * objects variable block (TODO: is this true? If yes, what a waste).
 * To mark the virtual inherit, the variable types receive the modifier
 * TYPE_MOD_VIRTUAL.
 * However, accesses to virtually inherited variables only use the first
 * inherit instance - if the variable index in the child's program code indexes
 * the variable block associated with a later instance, the driver finds
 * the first instance by comparing the .prog entries and uses the 'real'
 * variable associated with this first instance.
 *
 * TODO: Describe the inheritance relationship in the global comments.
 * TODO: Find a way to avoid the virtual var searching - either by encoding the
 * TODO:: inherit index in the code, or by adding a ref to the 'first
 * TODO:: instance' to this structure. See interpret.c:find_virtual_value().
 */

struct inherit
{
    struct program *prog;  /* Pointer to the inherited program */
    unsigned short function_index_offset;
      /* Offset of the inherited program's function block within the 
       * inheriting program's function block.
       */
    unsigned short variable_index_offset;
      /* Offset of the inherited program's variables block within the
       * inheriting program's variable block.
       */
    SBool is_extra; /* TRUE for duplicate virtual inherits */
    /* TODO: Why no flag for 'virtual inherited'? */
};

struct program
{
    p_int ref;                                /* Reference count */
    p_int total_size;                        /* Sum of all data in this struct */
#ifdef DEBUG
    p_int extra_ref;                        /* Used to verify ref count */
#endif
    bytecode_t *program;                        /* The binary instructions */
    char *name; /* Name of file that defined prog (allocated, no leading '/') */
    int32  id_number;                        /* used to associate information with
                                          this prog block without needing to
                                           increase the reference count     */
    int32  load_time;                        /* When has it been compiled ? */
    /*unsigned*/ char *line_numbers;        /* Line number information */
    unsigned short *function_names;
#define PROGRAM_END(program) ((bytecode_p)(program).function_names)
    /* TODO: funflags */ uint32 *functions;
      /* Array [.num_functions] with the flags and addresses of all
       * functions, inherited and own.
       * TODO: Instead of hiding the function information in the bytecode
       * TODO:: it should be tabled here.
       */
    char **strings;
      /* Array [.num_strings] of the shared strings used by the program */
    struct variable *variable_names;
      /* Array [.num_variables] with the flags, types and names of all
       * variables.
       */
    struct inherit *inherit;
      /* Array [.num_inherited] of descriptors for inherited programs.
       */
    unsigned short flags;
    short heart_beat;  /* Index of the heart beat function.
                        * -1 means no heart beat
                        */
    /*
     * The types of function arguments are saved where 'argument_types'
     * points. It can be a variable number of arguments, so allocation
     * is done dynamically. To know where first argument is found for
     * function 'n' (number of function), use 'type_start[n]'.
     * These two arrays will only be allocated if '#pragma save_types' has
     * been specified. This #pragma should be specified in files that are
     * commonly used for inheritance. There are several lines of code
     * that depends on the type length (16 bits) of 'type_start' (sorry !).
     */
    unsigned short *argument_types;
#define INDEX_START_NONE                65535
    unsigned short *type_start;

    p_int swap_num;            /* Swap file offset. -1 is not swapped yet. */

    /*
     * And now some general size information.
     */
    unsigned short num_function_names;
    unsigned short num_functions;
      /* Number of functions (inherited and own) of this program */
    unsigned short num_strings;
      /* Number of shared strings used by the program */
    unsigned short num_variables;
      /* Number of variables (inherited and own) of this program */
    unsigned short num_inherited;
      /* Number of inherited programs */
};

/* --- Type Constants ---
 * 
 * Available types, with the number '0' being valid as any type.
 * The types are used just by the compiler when type checks are
 * enabled.
 */

/* Basic type values */

#define TYPE_UNKNOWN        0   /* This type must be casted */
#define TYPE_NUMBER         1
#define TYPE_STRING         2
#define TYPE_VOID           3
#define TYPE_OBJECT         4
#define TYPE_MAPPING        5
#define TYPE_FLOAT          6
#define TYPE_ANY            7   /* Will match any type */
#define TYPE_SPACE          8   /* TODO: ??? */
#define TYPE_CLOSURE        9
#define TYPE_SYMBOL        10
#define TYPE_QUOTED_ARRAY  11
#define TYPE_TERM          12

#define TYPEMAP_SIZE       13   /* Number of types */

/* Flags, or'ed on top of the basic type */

#define TYPE_MOD_POINTER    0x0040  /* Pointer to a basic type */
#define TYPE_MOD_REFERENCE  0x0080  /* Reference to a type */

#define TYPE_MOD_MASK   0x000000ff
  /* Mask for basic type and flags.
   */

#define TYPE_MOD_RMASK  (TYPE_MOD_MASK & ~TYPE_MOD_REFERENCE)
  /* Mask to delete TYPE_MOD_REFERENCE from a type value
   */

#define P_REPLACE_ACTIVE    0x0001 /* TODO: ??? Program replacement scheduled */

/* TODO: I don't think the driver hooks belong into here */
#define H_MOVE_OBJECT0        0
#define H_MOVE_OBJECT1        1
#define H_LOAD_UIDS        2
#define H_CLONE_UIDS        3
#define H_CREATE_SUPER        4
#define H_CREATE_OB        5
#define H_CREATE_CLONE        6
#define H_RESET                7
#define H_CLEAN_UP        8
#define H_MODIFY_COMMAND   9
#define H_NOTIFY_FAIL          10
#define H_NO_IPC_SLOT          11
#define H_INCLUDE_DIRS          12
#define H_TELNET_NEG          13
#define H_NOECHO          14
#define H_ERQ_STOP          15
#define H_MODIFY_COMMAND_FNAME 16
#define NUM_CLOSURE_HOOKS 17

#define VIRTUAL_VAR_TAG 0x4000


/* --- struct instr: description of stackmachine instructions ---
 *
 * Stackmachine instructions are both 'internal' codes with no external
 * representation, as well as efuns.
 *
 * The information about all instructions is collected in the table
 * instrs[] which is indexed by the bytecode of the instructions.
 *
 * The table is declared in instrs.h and defined in the file efun_defs.c
 * both of which are created by make_func from the func_spec file.
 * The table is compiled into the lexer module and exported from there.
 */

struct instr
{
    short max_arg;    /* Maximum number of arguments, -1 for '...' */
    short min_arg;    /* Minimum number of arguments. */
    char  type[2];    /* Types of arguments 1 and 2. */
    short Default;
      /* An efun to use as default value for last argument.
       * > 0: index into instrs[] to the efun to use.
       *   0: no default value.
       *  -1: this whole entry describes an internal stackmachine code,
       *      not a normal efun (an 'operator' in closure lingo).
       */
    short ret_type;   /* The return type used by the compiler. */
    short arg_index;  /* Indexes the efun_arg_types[] array (see make_func). */
    char *name;       /* The printable name of the instruction. */
};

#endif /* __EXEC_H__ */
