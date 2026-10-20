"use strict";

// The documentation stays readable without JavaScript. These are enhancements.
document.documentElement.classList.add("js-enabled");

const ThemeToggle = document.querySelector(".theme-toggle");
function UpdateThemeLabel() {
    const IsDark = document.documentElement.dataset.theme === "dark";
    ThemeToggle?.setAttribute("aria-label", `Switch to ${IsDark ? "light" : "dark"} theme`);
    ThemeToggle?.setAttribute("aria-pressed", String(IsDark));
}
if (ThemeToggle) {
    ThemeToggle.hidden = false;
    UpdateThemeLabel();
    ThemeToggle.addEventListener("click", () => {
        const Theme = document.documentElement.dataset.theme === "dark" ? "light" : "dark";
        document.documentElement.dataset.theme = Theme;
        try { localStorage.setItem("astra-theme", Theme); } catch { /* Storage is optional. */ }
        UpdateThemeLabel();
    });
}

const Menu = document.querySelector(".mobile-toggle");
const Sidebar = document.querySelector(".sidebar");
if (Menu && Sidebar) {
    Menu.hidden = false;
    Menu.addEventListener("click", () => {
        const IsOpen = Sidebar.classList.toggle("is-open");
        Menu.setAttribute("aria-expanded", String(IsOpen));
        Menu.textContent = IsOpen ? "Close" : "Menu";
    });
    function CloseMenu() {
        Sidebar.classList.remove("is-open");
        Menu.setAttribute("aria-expanded", "false");
        Menu.textContent = "Menu";
    }
    Sidebar.addEventListener("click", Event => { if (Event.target.closest("a")) { CloseMenu(); } });
    document.addEventListener("keydown", Event => {
        if (Event.key === "Escape" && Sidebar.classList.contains("is-open")) {
            CloseMenu();
            Menu.focus();
        }
    });
}

for (const Block of document.querySelectorAll(".code-block")) {
    const Button = Block.querySelector(".copy-code");
    const Code = Block.querySelector("code");
    if (!Button || !Code) { continue; }
    const Label = Button.getAttribute("aria-label");
    let FeedbackTimer;
    Button.hidden = false;
    Button.addEventListener("click", async () => {
        try {
            await navigator.clipboard.writeText(Code.textContent);
            Button.textContent = "Copied";
            Button.setAttribute("aria-label", "Code copied");
        } catch {
            const Range = document.createRange();
            Range.selectNodeContents(Code);
            const Selection = window.getSelection();
            Selection.removeAllRanges();
            Selection.addRange(Range);
            Button.textContent = "Selected";
            Button.setAttribute("aria-label", "Code selected. Press Ctrl+C or Command+C to copy");
        }
        window.clearTimeout(FeedbackTimer);
        FeedbackTimer = window.setTimeout(() => {
            Button.textContent = "Copy";
            Button.setAttribute("aria-label", Label);
        }, 1600);
    });

}

// Tokenize displayed text without interpreting source as HTML.
function Highlight(Code) {
    if (Code.classList.contains("language-luau") || Code.classList.contains("language-lua")) {
        const Tokens = /(--\[(=*)\[[\s\S]*?\]\2\](?:--)?|--[^\n]*|\[(=*)\[[\s\S]*?\]\3\]|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|\b(?:and|break|continue|do|else|elseif|end|export|false|for|function|if|in|local|nil|not|or|repeat|return|then|true|type|until|while)\b|\b\d+(?:\.\d+)?(?:e[+-]?\d+)?\b|\b[A-Za-z_]\w*(?=\s*\())/g;
        const Text = Code.textContent;
        const Fragment = document.createDocumentFragment();
        let Offset = 0;
        for (const Match of Text.matchAll(Tokens)) {
            Fragment.append(document.createTextNode(Text.slice(Offset, Match.index)));
            const Span = document.createElement("span");
            const Value = Match[0];
            Span.className = Value.startsWith("--") ? "token-comment" : /^["'\[]/.test(Value) ? "token-string" : /^\d/.test(Value) ? "token-number" : /^(and|break|continue|do|else|elseif|end|export|false|for|function|if|in|local|nil|not|or|repeat|return|then|true|type|until|while)$/.test(Value) ? "token-keyword" : "token-function";
            Span.textContent = Value;
            Fragment.append(Span);
            Offset = Match.index + Value.length;
        }
        Fragment.append(document.createTextNode(Text.slice(Offset)));
        Code.replaceChildren(Fragment);
    }
}

for (const Code of document.querySelectorAll("pre code")) { Highlight(Code); }

const ExampleTabs = document.querySelector(".example-tabs");
if (ExampleTabs) {
    const Tabs = [...ExampleTabs.querySelectorAll('[role="tab"]')];
    const Panes = [...document.querySelectorAll(".example-pane")];
    function SelectExample(Tab, Animate = true) {
        for (const Item of Tabs) {
            const Selected = Item === Tab;
            Item.setAttribute("aria-selected", String(Selected));
            Item.tabIndex = Selected ? 0 : -1;
        }
        for (const Pane of Panes) {
            const Selected = Pane.id === Tab.getAttribute("aria-controls");
            Pane.hidden = !Selected;
            Pane.classList.toggle("is-entering", Selected && Animate);
        }
    }
    ExampleTabs.hidden = false;
    SelectExample(Tabs.find(Tab => Tab.getAttribute("aria-selected") === "true"), false);
    for (const Tab of Tabs) {
        Tab.addEventListener("click", () => SelectExample(Tab));
        Tab.addEventListener("keydown", Event => {
            let Index = Tabs.indexOf(Tab);
            if (Event.key === "ArrowRight") { Index = (Index + 1) % Tabs.length; }
            else if (Event.key === "ArrowLeft") { Index = (Index + Tabs.length - 1) % Tabs.length; }
            else if (Event.key === "Home") { Index = 0; }
            else if (Event.key === "End") { Index = Tabs.length - 1; }
            else { return; }
            Event.preventDefault();
            SelectExample(Tabs[Index]);
            Tabs[Index].focus();
        });
    }
}

const Search = document.querySelector(".search-dialog");
const Trigger = document.querySelector(".search-trigger");
const Input = document.querySelector("#search-input");
const Results = document.querySelector(".search-results");
const Status = document.querySelector(".search-status");
let SearchPages;
let SearchLoad;

function ShowResults() {
    const Words = Input.value.trim().toLowerCase().split(/\s+/).filter(Boolean);
    const Ranked = (SearchPages || []).map(Page => {
        const Title = Page.title.toLowerCase();
        const Headings = Page.headings.join(" ").toLowerCase();
        const Haystack = (Page.title + " " + Page.headings.join(" ") + " " + Page.text).toLowerCase();
        const Matches = Words.every(Word => Haystack.includes(Word));
        const Score = Words.reduce((Total, Word) => Total + (Title.includes(Word) ? 8 : 0) + (Headings.includes(Word) ? 4 : 0) + Math.min(3, Haystack.split(Word).length - 1), 0);
        return { Page, Matches, Score };
    }).filter(Item => Item.Matches).sort((A, B) => B.Score - A.Score).slice(0, 12);

    Results.replaceChildren();
    for (const { Page } of Ranked) {
        const Item = document.createElement("li");
        const Link = document.createElement("a");
        Link.href = Page.url;
        const Title = document.createElement("strong");
        Title.textContent = Page.title;
        const Excerpt = document.createElement("span");
        const Position = Words.length ? Math.max(0, Page.text.toLowerCase().indexOf(Words[0]) - 50) : 0;
        Excerpt.textContent = (Position ? "…" : "") + Page.text.slice(Position, Position + 145) + "…";
        Link.append(Title, Excerpt);
        Item.append(Link);
        Results.append(Item);
    }
    Status.textContent = Ranked.length ? `${Ranked.length} ${Ranked.length === 1 ? "page" : "pages"}${Ranked.length === 12 ? " shown" : ""}` : "No matching pages. Try an option name or a shorter query.";
}

async function OpenSearch() {
    if (!Search.open) { Search.showModal(); }
    Input.focus();
    if (!SearchPages) {
        Status.textContent = "Loading documentation…";
        try {
            SearchLoad ||= fetch("search-index.json").then(Response => {
                if (!Response.ok) { throw new Error("Search index unavailable"); }
                return Response.json();
            });
            SearchPages = await SearchLoad;
        } catch {
            SearchLoad = undefined;
            Status.textContent = "Search is unavailable. Use the documentation navigation to browse pages.";
            return;
        }
    }
    ShowResults();
}

if (Search && typeof Search.showModal === "function") {
    Trigger.hidden = false;
    Trigger.addEventListener("click", OpenSearch);
    document.querySelector(".close-search").addEventListener("click", () => Search.close());
    Search.addEventListener("close", () => Trigger.focus());
    Search.addEventListener("click", Event => { if (Event.target === Search) { Search.close(); } });
    Input.addEventListener("input", ShowResults);
    Input.addEventListener("keydown", Event => {
        if (Event.key === "ArrowDown") {
            Event.preventDefault();
            Results.querySelector("a")?.focus();
        }
        if (Event.key === "Enter") { Results.querySelector("a")?.click(); }
    });
    Results.addEventListener("keydown", Event => {
        const Links = [...Results.querySelectorAll("a")];
        const Position = Links.indexOf(document.activeElement);
        if (Event.key === "ArrowDown") {
            Event.preventDefault();
            Links[Math.min(Position + 1, Links.length - 1)]?.focus();
        } else if (Event.key === "ArrowUp") {
            Event.preventDefault();
            if (Position <= 0) { Input.focus(); } else { Links[Position - 1]?.focus(); }
        }
    });
    document.addEventListener("keydown", Event => {
        if (Event.key === "Escape" && Search.open) {
            Event.preventDefault();
            Search.close();
        } else if ((Event.ctrlKey || Event.metaKey) && Event.key.toLowerCase() === "k") {
            Event.preventDefault();
            if (Search.open) { Search.close(); } else { OpenSearch(); }
        }
    });
}

const Contents = [...document.querySelectorAll(".contents ul a")];
if ("IntersectionObserver" in window && Contents.length) {
    const ById = new Map(Contents.map(Link => [Link.hash.slice(1), Link]));
    const Observer = new IntersectionObserver(Entries => {
        const Visible = Entries.filter(Entry => Entry.isIntersecting);
        if (!Visible.length) { return; }
        Contents.forEach(Link => Link.removeAttribute("aria-current"));
        ById.get(Visible[0].target.id)?.setAttribute("aria-current", "location");
    }, { rootMargin: "-80px 0px -65% 0px" });
    for (const Id of ById.keys()) {
        const Heading = document.getElementById(Id);
        if (Heading) { Observer.observe(Heading); }
    }
}
