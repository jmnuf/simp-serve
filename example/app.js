class E_Signal {
  #listeners;
  constructor(value) {
    this.#listeners = [];
    Object.defineProperty(this, Symbol.for("value"), {
      configurable: true,
      enumerable: false,
      writable: true,
      value,
    });
  }
  listen(fn) {
    this.#listeners.push(fn);
  }
  mapped(mapper) {
    const mapped_value = mapper(this[Symbol.for("value")]);
    const sig = new E_Signal(mapped_value);
    Object.defineProperty(sig, "value", {
      configurable: false,
      enumerable: true,
      get() {
        return this[Symbol.for("value")];
      },
      set: null,
    });
    this.listen((value) => sig[Symbol.for("value")] = mapper(value));
    return sig;
  }
  get value() {
    return this[Symbol.for("value")];
  }
  set value(new_val) {
    if (this[Symbol.for("value")] == new_val) return;
    this[Symbol.for("value")] = new_val;
    for (const fn of this.#listeners) {
      fn(new_val);
    }
  }
  cleanup() {
    this.#listeners.length = 0;
  }
}

function _E_render_children(children) {
  const frag = document.createDocumentFragment();
  if (!children) return frag;
  if (!Array.isArray(children)) children = [children];
  for (const child of children) {
    if (child == null) continue;

    if (Array.isArray(child)) {
      const subfrag = _E_render_children(child);
      frag.append(subfrag);
      continue;
    }

    if (typeof child == "function") {
      const node = E(child);
      frag.append(node);
      continue;
    }

    if (child instanceof E_Signal) {
      const node = document.createTextNode(`${child.value}`);
      child.listen((value) => node.data = `${value}`);
      frag.append(node);
      continue;
    }

    if (child instanceof Node) {
      frag.append(child);
      continue;
    }

    const node = document.createTextNode(`${child}`);
    frag.append(node);
  }

  return frag;
}

const E = (elem_or_tag, props = {}) => {
  if (typeof elem_or_tag == "function") {
    return elem_or_tag(props);
  }
  if (!elem_or_tag) throw new TypeError("Missing to provide an element or tag");

  let elem;
  if (typeof elem_or_tag == "string") {
    elem = document.createElement(elem_or_tag);
  } else {
    elem = elem_or_tag;
  }
  const def_addEventListener = elem.addEventListener.bind(elem);
  elem[Symbol.for("@listeners")] = elem[Symbol.for("@listeners")] ?? {};
  elem.addEventListener = function(event_name, fn, opt) {
    const events = elem[Symbol.for("@listeners")][event_name] ?? [];
    events.push({ fn, opt });
    elem[Symbol.for("@listeners")][event_name] = events;
    return def_addEventListener(event_name, fn, opt);
  }
  const children = props.children != null ? Array.isArray(props.children) ? props.children : [props.children] : [];
  for (const key of Object.keys(props)) {
    if (key == "children") continue;
    if (key == "@ref") {
      props[key] = elem;
      continue;
    }
    const val = props[key];

    if (key.startsWith("on")) {
      const event_name = key.substring(2).toLowerCase();
      elem.addEventListener(event_name, val);
      continue;
    }

    if (key in elem) {
      if (elem[key] == null) {
        delete elem[key];
        continue;
      }
      if (val instanceof E_Signal) {
        val.listen((value) => {
          elem[key] = value;
        });
        elem[key] = val.value;
      } else {
        elem[key] = val;
      }
      continue;
    }

    if (val instanceof E_Signal) {
      val.listen((value) => {
        elem.setAttribute(key, value);
      });
      elem.setAttribute(key, val.value);
    } else {
      elem.setAttribute(key, val);
    }
  }

  const frag = _E_render_children(children);
  elem.append(frag);

  return elem;
}
const signal = (value) => new E_Signal(value);
function node_remove_children(node) {
  while (node.firstChild) {
    node.removeChild(node.firstChild);
  }
}

class Todo {
  constructor(name, done) {
    this._name = signal(name);
    this._done = signal(done);
  }

  get name() {
    return this._name.value;
  }
  set name(name) {
    if (typeof name != "string") return;
    this._name.value = name;
  }

  get done() {
    return this._done.value;
  }
  set done(done) {
    if (typeof done != "boolean") return;
    this._done.value = done;
  }

  render(on_delete) {
    const self = this;
    return E("li", {
      children: [
        E("label", {
          children: [
            E("input", {
              type: "checkbox",
              checked: this._done,
              onInput(ev) {
                self.done = ev.target.checked;
              }
            }),
            " ",
            this._name,
          ],
        }),
        E("button", {
          style: "border: none; background: none; color: red; font-weight: bolder;",
          children: "X",
          onClick(ev) {
            const element = ev.target.parentElement;
            on_delete({ element, todo: self });
          },
        }),
      ]
    });
  }

  static create(name, done) {
    return new Todo(name, done);
  }
}

const appDiv = document.getElementById("app");

(function App(app) {
  let ul;

  const todos = [
    Todo.create("Setup Example", true),
    Todo.create("Don't vibe code", true),
    Todo.create("Become good at chess", false),
  ];
  const on_todo_delete = ({ element, todo }) => {
    const idx = todos.indexOf(todo);
    if (idx < 0) return;
    todos.splice(idx, 1);
    element.remove();
  };
  E(app, {
    children: [
      E("h2", { children: "Example Counter:" }),
      Counter(),
      
      E("h2", { children: "Example Todos:" }),
      E("form", {
        children: [
          E("input", { type: "text", name: "todo-name", required: true, }),
          E("input", { type: "submit", name: "submit-btn", value: "create", }),
        ],
        onSubmit(ev) {
          ev.preventDefault();
          if (!ev.target.reportValidity()) return;
          const fdata = new FormData(ev.target);
          const name = fdata.get("todo-name");
          if (!name) return;
          ev.target.reset();
          const item = Todo.create(name, false);
          todos.push(item);
          ul.append(item.render(on_todo_delete));
        },
      }),
      E("ul", {
        style: "list-style: none; margin-left: 0; padding-left: 5px;",
        get ["@ref"]() {
          return ul;
        },
        set ["@ref"](v) {
          ul = v;
        },
        children: todos.map((t) => t.render(on_todo_delete)),
      }),
    ]
  });
})(appDiv);

function Counter() {
  const count = signal(0);
  const gen_label = () => ["Click count is ", count];
  return E("button", {
    children: gen_label(),
    onClick(ev) {
      count.value++;
    },
  });
}

