"use strict";

// The documentation stays readable without JavaScript. These are enhancements.
document.documentElement.classList.add("js-enabled");

const Menu = document.querySelector(".mobile-toggle");
const Sidebar = document.querySelector(".sidebar");
if (Menu && Sidebar) {
    Menu.hidden = false;
    Menu.addEventListener("click", () => {
        const IsOpen = Sidebar.classList.toggle("is-open");
        Menu.setAttribute("aria-expanded", String(IsOpen));
        Menu.textContent = IsOpen ? "Close" : "Menu";
    });
}

for (const Block of document.querySelectorAll(".code-block")) {
    const Button = Block.querySelector(".copy-code");
    const Code = Block.querySelector("code");
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

    // Tokenize only the displayed text; never interpret source as HTML.
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
        const Haystack = (Page.title + " " + Page.headings.join(" ") + " " + Page.text).toLowerCase();
        const Matches = Words.every(Word => Haystack.includes(Word));
        const Score = Words.reduce((Total, Word) => Total + (Title.includes(Word) ? 5 : 0), 0);
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
    Search.showModal();
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
    Search.addEventListener("click", Event => { if (Event.target === Search) { Search.close(); } });
    Input.addEventListener("input", ShowResults);
    Input.addEventListener("keydown", Event => {
        if (Event.key === "ArrowDown") {
            Event.preventDefault();
            Results.querySelector("a")?.focus();
        }
        if (Event.key === "Enter") { Results.querySelector("a")?.click(); }
    });
    document.addEventListener("keydown", Event => {
        if ((Event.ctrlKey || Event.metaKey) && Event.key.toLowerCase() === "k") {
            Event.preventDefault();
            if (Search.open) { Search.close(); } else { OpenSearch(); }
        }
    });
}

const Contents = [...document.querySelectorAll(".contents a")];
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
