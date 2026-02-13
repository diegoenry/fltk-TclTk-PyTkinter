# -- Project information -------------------------------------------------------
project = "FLTK Console"
author = "FLTK Console Contributors"
copyright = "2025, FLTK Console Contributors"

# -- General configuration -----------------------------------------------------
extensions = [
    "myst_parser",
    "sphinxcontrib.mermaid",
]

# MyST: enable all features used in our Markdown docs.
myst_enable_extensions = [
    "colon_fence",     # ::: directive syntax
    "fieldlist",       # field lists
    "deflist",         # definition lists
    "tasklist",        # - [x] checkboxes
]
myst_heading_anchors = 3  # auto-generate anchors for h1-h3
myst_fence_as_directive = {"mermaid"}  # ```mermaid blocks → {mermaid} directives

# Mermaid: use the CDN-based JS renderer (no local install required).
mermaid_version = "11"
mermaid_init_js = """
mermaid.initialize({
    startOnLoad: true,
    theme: "dark",
    themeVariables: {
        darkMode: true,
        background: "#1a1a2e",
        primaryColor: "#0f3460",
        primaryTextColor: "#e0e0e0",
        lineColor: "#00dc78",
        secondaryColor: "#16213e",
        tertiaryColor: "#1a1a2e"
    }
});
"""

# Source files.
source_suffix = {
    ".rst": "restructuredtext",
    ".md": "markdown",
}

# -- HTML output ---------------------------------------------------------------
html_theme = "furo"
html_title = "FLTK Console"

html_theme_options = {
    "dark_css_variables": {
        "color-brand-primary": "#00dc78",
        "color-brand-content": "#00dc78",
    },
    "light_css_variables": {
        "color-brand-primary": "#0a8a50",
        "color-brand-content": "#0a8a50",
    },
    "sidebar_hide_name": False,
    "navigation_with_keys": True,
}

# Don't copy source .md files into the build.
html_copy_source = False
html_show_sourcelink = False
