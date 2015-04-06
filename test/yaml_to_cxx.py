from sys import argv, stderr
import sys
from yaml import load
from os import walk
from os.path import join
from re import sub, match
from collections import namedtuple
import ast

class Discard(Exception):
    pass

class Unhandled(Exception):
    pass

failed = False

Ctx = namedtuple('Ctx', ['vars', 'context', 'type'])

def convert(python, prec, file, type):
    try:
        expr = ast.parse(python, filename=file, mode='eval').body
        cxx = to_cxx(expr, prec, Ctx(vars=[], type=type, context=None))
        return sub('" \\+ "', '', cxx)
    except Unhandled:
        print("While translating: " + python, file=stderr)
        raise
    except SyntaxError as e:
        raise Unhandled("syntax error: " + str(e) + ": " + python)

def py_str(py):
    if not isinstance(py, "".__class__):
            return repr(py)
    return py

def rename(id):
    return {
        'default': 'default_',
        'do': 'do_',
        'union': 'union_',
        'False': 'false',
        'True': 'true',
        'xrange': 'R::range',
        'None': 'R::Nil()',
        'null': 'R::Nil()',
        'delete': 'delete_'
    }.get(id, id)

def to_cxx_str(expr):
    if type(expr) is ast.Str:
        return string(expr.s)
    if type(expr) is ast.Num:
        return string(str(expr.n))
    if 'frozenset' in ast.dump(expr):
        raise Discard
    raise Unhandled("not string expr: " + ast.dump(expr))

def is_null(expr):
    return type(expr) is ast.Name and expr.id in ['None', 'null']

def to_cxx_expr(expr, prec, ctx):
    if ctx.type is 'query':
        if type(expr) in [ast.Str, ast.Num] or is_null(expr):
            return "R::expr(" + to_cxx(expr, 17, ctx) + ")"
    return to_cxx(expr, prec, ctx)
def to_cxx(expr, prec, ctx):
    context = ctx.context
    ctx = Ctx(vars=ctx.vars, type=ctx.type, context=None)
    try:
        t = type(expr)
        if t == ast.Num:
            return repr(expr.n)
        elif t == ast.Call:
            assert not expr.kwargs
            assert not expr.starargs
            return to_cxx(expr.func, 2, ctx_set(ctx, context='function')) + to_args(expr.func, expr.args, expr.keywords, ctx)
        elif t == ast.Attribute:
            if type(expr.value) is ast.Name and expr.value.id == 'r':
                if expr.attr == 'error' and context is not 'function':
                    return "R::error()"
                return "R::" + rename(expr.attr)
            else:
                return to_cxx(expr.value, 2, ctx) + "." + rename(expr.attr)
        elif t == ast.Name:
            if expr.id in ['frozenset']:
                raise Discard()
            elif expr.id in ctx.vars:
                return parens(prec, 3, "*" + expr.id)
            return rename(expr.id)
        elif t == ast.Subscript:
            st = type(expr.slice)
            if st == ast.Index:
                return to_cxx(expr.value, 2, ctx) + "[" + to_cxx(expr.slice.value, 17, ctx) + "]"
            if st == ast.Slice:
                assert not expr.slice.step
                if not expr.slice.upper:
                    return to_cxx(expr.value, 2, ctx) + ".slice(" + to_cxx(expr.slice.lower, 17, ctx) + ")"
                if not expr.slice.lower:
                    return to_cxx(expr.value, 2, ctx) + ".limit(" + to_cxx(expr.slice.upper, 17, ctx) + ")"
                return to_cxx(expr.value, 2, ctx) + ".slice(" + to_cxx(expr.slice.lower, 17, ctx) + ", " + to_cxx(expr.slice.upper, 17, ctx) + ")"
            else:
                raise Unhandled("slice type: " + repr(st))
        elif t == ast.Dict:
            if ctx.type is 'query':
                return "R::object(" + ', '.join([to_cxx(k, 17, ctx) + ", " + to_cxx(v, 17, ctx) for k, v in zip(expr.keys, expr.values)]) + ")"
            else:
                return "R::Object{" + ', '.join(["{" + to_cxx_str(k) + "," + to_cxx(v, 17, ctx) + "}" for k, v in zip(expr.keys, expr.values)]) + "}"
        elif t == ast.Str:
            return string(expr.s)
        elif t == ast.List:
            if ctx.type is 'query':
                return "R::array(" + ', '.join([to_cxx(el, 17, ctx) for el in expr.elts]) + ")"
            else:
                return "R::Array{" + ', '.join([to_cxx(el, 17, ctx) for el in expr.elts]) + "}"
        elif t == ast.Lambda:
            assert not expr.args.vararg
            assert not expr.args.kwarg
            ctx = ctx_set(ctx, vars = ctx.vars + [arg.arg for arg in expr.args.args])
            return "[=](" + ', '.join(['R::Var ' + arg.arg for arg in expr.args.args]) + "){ return " + to_cxx_expr(expr.body, 17, ctx) + "; }"
        elif t == ast.BinOp:
            if type(expr.op) is ast.Mult and type(expr.left) is ast.Str:
                return "repeat(" + to_cxx(expr.left, 17, ctx) + ", " + to_cxx(expr.right, 17, ctx) + ")"
            op, op_prec = convert_op(expr.op)
            if op_prec: 
                return parens(prec, op_prec, to_cxx_expr(expr.left, op_prec, ctx) + " " + op + " " + to_cxx(expr.right, op_prec, ctx))
            else:
                return op + "(" + to_cxx(expr.left, 17, ctx) + ", " + to_cxx(expr.right, 17, ctx) + ")"
        elif t == ast.ListComp:
            assert len(expr.generators) == 1
            assert type(expr.generators[0]) == ast.comprehension
            assert type(expr.generators[0].target) == ast.Name
            seq = to_cxx(expr.generators[0].iter, 2, ctx)
            var = expr.generators[0].target.id
            body = to_cxx(expr.elt, 17, ctx_set(ctx, vars = ctx.vars + [var]))
            return seq + ".map([=](R::Var " + var + "){ return " + body + "; })"
        elif t == ast.Compare:
            assert len(expr.ops) == 1
            assert len(expr.comparators) == 1
            op, op_prec = convert_op(expr.ops[0]) 
            return parens(prec, op_prec, to_cxx(expr.left, op_prec, ctx) + op + to_cxx(expr.comparators[0], op_prec, ctx))
        elif t == ast.UnaryOp:
            op, op_prec = convert_op(expr.op)
            return parens(prec, op_prec, op + to_cxx(expr.operand, op_prec, ctx))
        elif t == ast.Bytes:
            return string(expr.s)
        elif t == ast.Tuple:
            return "R::Array{" + ', '.join([to_cxx(el, 17, ctx) for el in expr.elts]) + "}"
        else:
            raise Unhandled('ast type: ' + repr(t) + ', fields: ' + str(expr._fields))
    except Unhandled:
        print("While translating: " + ast.dump(expr), file=stderr)
        raise

def ctx_set(ctx, context=None, vars=None, type=None):
    if context is None:
        context = ctx.context
    if vars is None:
        vars = ctx.vars
    if type is None:
        type = ctx.type
    return Ctx(vars=vars, type=type, context=context)
    
def convert_op(op):
    t = type(op)
    if t == ast.Add:
        return '+', 6
    if t == ast.Sub:
        return '-', 6
    if t == ast.Mod:
        return '%', 5
    if t == ast.Mult:
        return '*', 5
    if t == ast.Div:
        return '/', 5
    if t == ast.Pow:
        return 'pow', 0
    if t == ast.Eq:
        return '==', 9
    if t == ast.NotEq:
        return '!=', 9
    if t == ast.Lt:
        return '<', 8
    if t == ast.Gt:
        return '>', 8
    if t == ast.GtE:
        return '>=', 8
    if t == ast.LtE:
        return '<=', 8
    if t == ast.USub:
        return '-', 3
    if t == ast.BitAnd:
        return '&&', 13
    if t == ast.BitOr:
        return '||', 14
    if t == ast.Invert:
        return '!', 3
    else:
        raise Unhandled('op type: ' + repr(t))

def to_args(func, args, optargs, ctx):
    ret = "("
    ret = ret + ', '.join([to_cxx(arg, 17, ctx) for arg in args])
    o = list(optargs)
    if o:
        out = []
        for f in o:
            out.append("{" + string(f.arg) + ", R::expr(" + to_cxx(f.value, 17, ctx) + ")}")
        if args:
            ret = ret + ", "
        ret = ret + "R::OptArgs{" + ', '.join(out) + "}"
    return ret + ")"

def string(s):
    if type(s) is str:
        def string_escape(c):
            if c == '"':
                return '\\"'
            if c == '\\':
                return '\\\\'
            if c == '\n':
                return '\\n'
            else:
                return c
    elif type(s) is bytes:
        def string_escape(c):
            if c < 32 or c > 127:
                return '\\x' + ('0' + hex(c)[2:])[-2:]
            if c == 34:
                return '\\"'
            if c == 92:
                return '\\\\'
            else:
                return chr(c)
    else:
        raise Unhandled("string type: " + repr(type(s)))
    return '"' + ''.join([string_escape(c) for c in s]) + '"'

def parens(prec, in_prec, cxx):
    if in_prec >= prec:
        return "(" + cxx + ")"
    else:
        return cxx
    
print("// auto-generated by yaml_to_cxx.py")
print("#include \"testlib.h\"")
print("void run_upstream_tests() {")

indent = 1

def p(s):
    print((indent * "    ") + s);

def enter(s = ""):
    if s:
        p(s)
    global indent
    indent = indent + 1

def exit(s = ""):
    global indent
    indent = indent - 1
    if s:
        p(s)

def get(o, ks, d):
    try:
        for k in ks:
            if k in o:
                return o[k]
    except:
        pass
    return d
    
def python_tests(tests):
    for test in tests:
        try:
            ot = py_str(get(test['ot'], ['py', 'cd'], test['ot']))
        except:
            ot = None
        if 'def' in test:
            py = get(test['def'], ['py', 'cd'], test['def'])
            if py and type(py) is not dict:
                yield py_str(py), None, 'def'
        else:
            py = get(test, ['py', 'cd'], None)
            if py:
                if isinstance(py, "".__class__):
                    yield py, ot, 'query'
                else:
                    for t in py:
                        yield py_str(t), ot, 'query'

skip = [
    'test_upstream_regression_1133', # python-only
    'test_upstream_times_api', # no time support yet
]

def maybe_discard(py, ot):
    if ot is None:
        return
    if match(".*Expected .* argument", ot):
        raise Discard

for dirpath, dirnames, filenames in walk(argv[1]):
    section_name = sub('/', '_', dirpath)
    p("enter_section(\"%s\");" % section_name)
    for file in sorted(filenames)[:1]: # TODO
        name = section_name + '_' + file.split('.')[0]
        if name in skip:
            continue
        data = load(open(join(dirpath, file)).read())
        enter("{")
        p("enter_section(\"%s: %s\");" % (file.split('.')[0], data['desc']))
        if 'table_variable_name' in data:
            for var in data['table_variable_name'].split():
                p("temp_table %s_table;" % var)
                p("R::Query %s = %s_table.table();" % (var, var)) 
        for py, ot, tp in python_tests(data["tests"]):
            try:
                maybe_discard(py, ot)
                assignment = match("^(\w+) *= *([^=].*)$", py)
                if assignment and tp == 'def':
                    p("auto " + assignment.group(1) + " = " + convert(assignment.group(2), 15, name, 'datum') + ";")
                elif assignment:
                    p("auto " + assignment.group(1) + " = " + convert(assignment.group(2), 15, name, 'query') + ".run_cursor(*conn);")
                elif ot:
                    p("TEST_EQ(%s.run(*conn), (%s));" % (convert(py, 2, name, 'query'), convert(ot, 17, name, 'datum')))
                else:
                    p("%s.run(*conn);" % convert(py, 2, name, 'query'))
            except Discard:
                pass
            except Unhandled as e:
                failed = True
                print("Could not translate: " + str(e), file=stderr)

        p("exit_section();") 
        exit("}")
    p("exit_section();")
        
print("}")

if failed:
    sys.exit(1)
