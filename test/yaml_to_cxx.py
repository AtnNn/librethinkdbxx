from sys import argv, stderr, exit
from yaml import load
from os import walk
from os.path import join
from re import sub, match
import ast

class Discard(Exception):
    pass

class Unhandled(Exception):
    pass

failed = False

def convert(python, prec, file):
    try:
        if not isinstance(python, "".__class__):
            python = repr(python)
        expr = ast.parse(python, filename=file, mode='eval').body
        return to_cxx(expr, prec, [])
    except Unhandled:
        print("While translating: " + python, file=stderr)
        raise
    except SyntaxError as e:
        raise Unhandled("syntax error: " + str(e) + ": " + python)

def rename(id):
    return {
        'default': 'default_',
        'do': 'do_',
        'union': 'union_',
        'False': 'false',
        'True': 'true',
        'null': 'R::Nil()'
    }.get(id, id)

def to_cxx(expr, prec, vars, context=None):
    try:
        t = type(expr)
        if t == ast.Num:
            return repr(expr.n)
        elif t == ast.Call:
            assert not expr.kwargs
            assert not expr.starargs
            return to_cxx(expr.func, 2, vars, 'function') + to_args(expr.func, expr.args, expr.keywords, vars)
        elif t == ast.Attribute:
            if type(expr.value) is ast.Name and expr.value.id == 'r':
                if expr.attr == 'error' and context is not 'function':
                    return "R::error()"
                return "R::" + rename(expr.attr)
            else:
                return to_cxx(expr.value, 2, vars) + "." + rename(expr.attr)
        elif t == ast.Name:
            if expr.id in ['frozenset']:
                raise Discard()
            elif expr.id in vars:
                return parens(prec, 3, "*" + expr.id)
            return rename(expr.id)
        elif t == ast.Subscript:
            st = type(expr.slice)
            if st == ast.Index:
                return to_cxx(expr.value, 2, vars) + "[" + to_cxx(expr.slice.value, 17, vars) + "]"
            if st == ast.Slice:
                assert not expr.slice.step
                if not expr.slice.upper:
                    return to_cxx(expr.value, 2, vars) + ".slice(" + to_cxx(expr.slice.lower, 17, vars) + ")"
                if not expr.slice.lower:
                    return to_cxx(expr.value, 2, vars) + ".limit(" + to_cxx(expr.slice.upper, 17, vars) + ")"
                return to_cxx(expr.value, 2, vars) + ".slice(" + to_cxx(expr.slice.lower, 17, vars) + "," + to_cxx(expr.slice.upper, 17, vars) + ")"
            else:
                raise Unhandled("slice type: " + repr(st))
        elif t == ast.Dict:
            return "R::Object{" + ', '.join(["{" + to_cxx(k, 17, vars) + "," + to_cxx(v, 17, vars) + "}" for k, v in zip(expr.keys, expr.values)]) + "}"
        elif t == ast.Str:
            return string(expr.s)
        elif t == ast.List:
            return "R::Array{" + ', '.join([to_cxx(el, 17, vars) for el in expr.elts]) + "}"
        elif t == ast.Lambda:
            assert not expr.args.vararg
            assert not expr.args.kwarg
            vars = vars + [arg.arg for arg in expr.args.args]
            return "[](" + ', '.join(['R::Var ' + arg.arg for arg in expr.args.args]) + "){ return " + to_cxx(expr.body, 17, vars) + "; }"
        elif t == ast.BinOp:
            op, op_prec = convert_op(expr.op)
            if op_prec: 
                return parens(prec, op_prec, to_cxx(expr.left, op_prec, vars) + op + to_cxx(expr.right, op_prec, vars))
            else:
                return op + "(" +  to_cxx(expr.left, 17, vars) + ", " + to_cxx(expr.right, 17, vars) + ")"
        elif t == ast.ListComp:
            assert len(expr.generators) == 1
            assert type(expr.generators[0]) == ast.comprehension
            assert type(expr.generators[0].target) == ast.Name
            seq = to_cxx(expr.generators[0].iter, 2, vars)
            var = expr.generators[0].target.id
            body = to_cxx(expr.elt, 17, vars + [var])
            return seq + ".map([](R::Var " + var + "){ return " + body + "})"
        elif t == ast.Compare:
            assert len(expr.ops) == 1
            assert len(expr.comparators) == 1
            op, op_prec = convert_op(expr.ops[0]) 
            return parens(prec, op_prec, to_cxx(expr.left, op_prec, vars) + op + to_cxx(expr.comparators[0], op_prec, vars))
        elif t == ast.UnaryOp:
            op, op_prec = convert_op(expr.op)
            return parens(prec, op_prec, op + to_cxx(expr.operand, op_prec, vars))
        elif t == ast.Bytes:
            return string(expr.s)
        elif t == ast.Tuple:
            return "R::Array{" + ', '.join([to_cxx(el, 17, vars) for el in expr.elts]) + "}"
        else:
            raise Unhandled('ast type: ' + repr(t) + ', fields: ' + str(expr._fields))
    except Unhandled:
        print("While translating: " + ast.dump(expr), file=stderr)
        raise

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

def to_args(func, args, optargs, vars):
    ret = "("
    ret = ret + ', '.join([to_cxx(arg, 17, vars) for arg in args])
    o = list(optargs)
    if o:
        out = []
        for f in o:
            out.append("{" + string(f.arg) + ", " + to_cxx(f.value, 17, vars))
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
                return '\\x' + ('0' + hex(c))[-2:]
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
            ot = get(test['ot'], ['py', 'cd'], test['ot'])
        except:
            ot = None
        if 'def' in test:
            py = get(test['def'], ['py', 'cd'], test['def'])
        else:
            py = get(test, ['py', 'cd'], None)
        if py:
            if isinstance(py, "".__class__):
                yield py, ot
            else:
                for t in py:
                    yield t, ot

skip = [
    'test_upstream_regression_1133'
]

for dirpath, dirnames, filenames in walk(argv[1]):
    section_name = sub('/', '_', dirpath)
    p("enter_section(\"%s\");" % section_name)
    for file in filenames:
        name = section_name + '_' + file.split('.')[0]
        if name in skip:
            continue
        data = load(open(join(dirpath, file)).read())
        enter("{")
        p("enter_section(\"%s\");" % data['desc'])
        if 'table_variable_name' in data:
            for var in data['table_variable_name'].split():
                p("Query %s(new_table());" % var)
        for py, ot in python_tests(data["tests"]):
            try:
                assignment = match("^(\w+) *= *([^=].*)$", py)
                if assignment:
                    p("R::Datum " + assignment.group(1) + " = " + convert(assignment.group(2), 15, name) + ".run_cursor(*conn);")
                elif ot:
                    p("TEST_EQ(%s.run(*conn), (%s));" % (convert(py, 2, name), convert(ot, 17, name)))
                else:
                    p("%s.run(*conn);" % convert(py, 2, name))
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
    exit(1)
