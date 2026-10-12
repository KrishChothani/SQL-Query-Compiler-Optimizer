/**
 * app.js — Frontend logic for SQL Query Compiler & Optimizer
 * Handles CodeMirror editor, table management, multi-stage rendering, and API communication.
 */

(function () {
    "use strict";

    // Application State
    let editor = null;
    let currentResultData = null;
    let activeLineHandle = null;

    // DOM Elements
    const elements = {
        themeToggleBtn: document.getElementById("btn-theme-toggle"),
        themeIcon: document.getElementById("theme-icon"),
        runBtn: document.getElementById("btn-run-query"),
        runSpinner: document.getElementById("run-spinner"),
        runIcon: document.getElementById("run-icon"),
        clearBtn: document.getElementById("btn-clear-editor"),
        optSwitch: document.getElementById("opt-switch"),
        modeSelect: document.getElementById("mode-select"),
        examplesMenu: document.getElementById("examples-dropdown-menu"),
        historyMenu: document.getElementById("history-dropdown-menu"),
        noHistoryItem: document.getElementById("no-history-item"),
        execTimeBadge: document.getElementById("execution-time-badge"),
        downloadCsvBtn: document.getElementById("btn-download-csv"),

        // Error Panel
        errorPanel: document.getElementById("error-panel"),
        errorCategoryBadge: document.getElementById("error-category-badge"),
        errorLocationBadge: document.getElementById("error-location-badge"),
        errorMessageText: document.getElementById("error-message-text"),

        // Results Tab
        resultCountBadge: document.getElementById("result-count-badge"),
        resultMetaNotice: document.getElementById("result-meta-notice"),
        resultStatusText: document.getElementById("result-status-text"),
        resultTruncatedNotice: document.getElementById("result-truncated-notice"),
        resultTotalRows: document.getElementById("result-total-rows"),
        resultThead: document.getElementById("result-table-thead"),
        resultTbody: document.getElementById("result-table-tbody"),

        // Tokens Tab
        tokensCountBadge: document.getElementById("tokens-count-badge"),
        tokensTbody: document.getElementById("tokens-table-tbody"),

        // AST & Plans
        astPre: document.getElementById("ast-pre"),
        planPre: document.getElementById("plan-pre"),
        optPlanPre: document.getElementById("opt-plan-pre"),
        rulesCountBadge: document.getElementById("rules-count-badge"),
        rulesFiredList: document.getElementById("rules-fired-list"),

        // Compare Tab
        reductionBadge: document.getElementById("reduction-badge"),
        compareReductionText: document.getElementById("compare-reduction-text"),
        compareReductionBadge: document.getElementById("compare-reduction-badge"),
        compareUnoptPlan: document.getElementById("compare-unopt-plan"),
        compareUnoptStats: document.getElementById("compare-unopt-stats"),
        unoptRowsProc: document.getElementById("unopt-rows-proc"),
        unoptTimeBadge: document.getElementById("unopt-time-badge"),
        compareOptPlan: document.getElementById("compare-opt-plan"),
        compareOptRules: document.getElementById("compare-opt-rules"),
        compareOptStats: document.getElementById("compare-opt-stats"),
        optRowsProc: document.getElementById("opt-rows-proc"),
        optTimeBadge: document.getElementById("opt-time-badge"),

        // Tables Sidebar
        tablesAccordion: document.getElementById("tables-accordion"),
        tableCountBadge: document.getElementById("table-count-badge"),
        tableSearchInput: document.getElementById("table-search-input"),
        refreshTablesBtn: document.getElementById("btn-refresh-tables"),
        tablesSpinner: document.getElementById("tables-loading-spinner"),
        noTablesMsg: document.getElementById("no-tables-msg"),
        resetWsNavBtn: document.getElementById("btn-reset-workspace-nav"),
        resetWsSidebarBtn: document.getElementById("btn-reset-workspace-sidebar"),

        // Modals & Forms
        formUploadCsv: document.getElementById("form-upload-csv"),
        uploadTableName: document.getElementById("upload-table-name"),
        uploadCsvFile: document.getElementById("upload-csv-file"),
        uploadErrorAlert: document.getElementById("upload-error-alert"),
        uploadSpinner: document.getElementById("upload-spinner"),
        btnSubmitUpload: document.getElementById("btn-submit-upload"),

        formCreateManual: document.getElementById("form-create-manual"),
        manualTableName: document.getElementById("manual-table-name"),
        columnBuilderContainer: document.getElementById("column-builder-container"),
        btnAddColRow: document.getElementById("btn-add-col-row"),
        manualRowsTextarea: document.getElementById("manual-rows-textarea"),
        manualPreviewThead: document.getElementById("manual-preview-thead"),
        manualPreviewTbody: document.getElementById("manual-preview-tbody"),
        manualErrorAlert: document.getElementById("manual-error-alert"),
        manualSpinner: document.getElementById("manual-spinner"),
        btnSubmitManual: document.getElementById("btn-submit-manual"),

        // Preview Modal
        modalTablePreview: document.getElementById("modal-table-preview"),
        previewModalTitle: document.getElementById("preview-modal-title"),
        previewModalRowCount: document.getElementById("preview-modal-row-count"),
        previewSchemaBadges: document.getElementById("preview-schema-badges"),
        previewShowingNotice: document.getElementById("preview-showing-notice"),
        previewTableThead: document.getElementById("preview-table-thead"),
        previewTableTbody: document.getElementById("preview-table-tbody"),
    };

    // ── 1. Theme Toggle ────────────────────────────────────────────────
    function initTheme() {
        const savedTheme = localStorage.getItem("sqlc_theme") || "dark";
        setTheme(savedTheme);

        elements.themeToggleBtn.addEventListener("click", () => {
            const current = document.documentElement.getAttribute("data-bs-theme");
            const target = current === "dark" ? "light" : "dark";
            setTheme(target);
        });
    }

    function setTheme(theme) {
        document.documentElement.setAttribute("data-bs-theme", theme);
        localStorage.setItem("sqlc_theme", theme);
        if (theme === "dark") {
            elements.themeIcon.className = "bi bi-moon-stars-fill text-warning";
            if (editor) editor.setOption("theme", "dracula");
        } else {
            elements.themeIcon.className = "bi bi-sun-fill text-warning";
            if (editor) editor.setOption("theme", "default");
        }
    }

    // ── 2. CodeMirror / Editor Initialization ─────────────────────────
    function initEditor() {
        const textarea = document.getElementById("sql-query-editor");
        if (window.CodeMirror) {
            const isDark = document.documentElement.getAttribute("data-bs-theme") === "dark";
            editor = CodeMirror.fromTextArea(textarea, {
                mode: "text/x-sql",
                theme: isDark ? "dracula" : "default",
                lineNumbers: true,
                lineWrapping: true,
                indentWithTabs: false,
                smartIndent: true,
                tabSize: 2,
                extraKeys: {
                    "Ctrl-Enter": () => runQuery(),
                    "Cmd-Enter": () => runQuery()
                }
            });

            editor.on("change", () => {
                clearErrorHighlight();
            });
        } else {
            // Textarea fallback
            textarea.addEventListener("keydown", (e) => {
                if ((e.ctrlKey || e.metaKey) && e.key === "Enter") {
                    e.preventDefault();
                    runQuery();
                }
            });
        }

        elements.clearBtn.addEventListener("click", () => {
            setQueryText("");
            clearError();
            if (editor) editor.focus();
        });
    }

    function getQueryText() {
        return editor ? editor.getValue() : document.getElementById("sql-query-editor").value;
    }

    function setQueryText(text) {
        if (editor) {
            editor.setValue(text);
        } else {
            document.getElementById("sql-query-editor").value = text;
        }
    }

    function clearErrorHighlight() {
        if (editor && activeLineHandle !== null) {
            editor.removeLineClass(activeLineHandle, "background", "cm-error-line");
            activeLineHandle = null;
        }
    }

    function highlightErrorLine(lineNum) {
        clearErrorHighlight();
        if (editor && lineNum > 0) {
            const lineIndex = lineNum - 1;
            if (lineIndex < editor.lineCount()) {
                activeLineHandle = editor.addLineClass(lineIndex, "background", "cm-error-line");
                editor.setCursor({ line: lineIndex, ch: 0 });
            }
        }
    }

    // ── 3. Query History ───────────────────────────────────────────────
    function getHistory() {
        try {
            return JSON.parse(localStorage.getItem("sqlc_query_history") || "[]");
        } catch {
            return [];
        }
    }

    function pushHistory(query) {
        if (!query || !query.trim()) return;
        const q = query.trim();
        let history = getHistory().filter(item => item !== q);
        history.unshift(q);
        if (history.length > 10) history = history.slice(0, 10);
        localStorage.setItem("sqlc_query_history", JSON.stringify(history));
        renderHistory();
    }

    function renderHistory() {
        const history = getHistory();
        const menu = elements.historyMenu;
        menu.querySelectorAll(".history-entry").forEach(el => el.remove());

        if (history.length === 0) {
            elements.noHistoryItem.classList.remove("d-none");
            return;
        }
        elements.noHistoryItem.classList.add("d-none");

        history.forEach(q => {
            const li = document.createElement("li");
            li.className = "history-entry";
            const a = document.createElement("a");
            a.className = "dropdown-item text-truncate font-monospace small";
            a.style.maxWidth = "320px";
            a.href = "#";
            a.textContent = q;
            a.addEventListener("click", (e) => {
                e.preventDefault();
                setQueryText(q);
                if (editor) editor.focus();
            });
            li.appendChild(a);
            menu.appendChild(li);
        });
    }

    // ── 4. Examples Loader ────────────────────────────────────────────
    async function loadExamples() {
        try {
            const resp = await fetch("/api/examples");
            const data = await resp.json();
            if (!data.examples) return;

            const menu = elements.examplesMenu;
            data.examples.forEach(ex => {
                const li = document.createElement("li");
                const a = document.createElement("a");
                a.className = "dropdown-item py-1";
                a.href = "#";
                a.innerHTML = `<div class="fw-semibold small">${escapeHtml(ex.title)}</div>
                               <div class="text-muted text-truncate font-monospace" style="font-size: 0.72rem; max-width: 320px;">${escapeHtml(ex.query)}</div>`;
                a.addEventListener("click", (e) => {
                    e.preventDefault();
                    setQueryText(ex.query);
                    if (ex.mode) elements.modeSelect.value = ex.mode;
                    if (editor) editor.focus();
                });
                li.appendChild(a);
                menu.appendChild(li);
            });
        } catch (err) {
            console.error("Failed to load examples", err);
        }
    }

    // ── 5. Run Query & Render Results ─────────────────────────────────
    async function runQuery() {
        const query = getQueryText().trim();
        if (!query) {
            showError({
                category: "Validation",
                message: "Query cannot be empty. Please enter a valid SQL query.",
                line: 0,
                col: 0
            });
            return;
        }

        clearError();
        setLoading(true);
        pushHistory(query);

        const mode = elements.modeSelect.value;
        const optimize = elements.optSwitch.checked;

        try {
            const resp = await fetch("/api/run", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({ query, mode, optimize })
            });

            const data = await resp.json();

            if (!resp.ok || data.status === "error") {
                showError(data);
            } else {
                renderOutput(data, mode);
            }
        } catch (err) {
            showError({
                category: "Runtime",
                message: `Network or server failure: ${err.message}`,
                line: 0,
                col: 0
            });
        } finally {
            setLoading(false);
        }
    }

    function setLoading(isLoading) {
        elements.runBtn.disabled = isLoading;
        if (isLoading) {
            elements.runSpinner.classList.remove("d-none");
            elements.runIcon.classList.add("d-none");
        } else {
            elements.runSpinner.classList.add("d-none");
            elements.runIcon.classList.remove("d-none");
        }
    }

    function showError(errData) {
        elements.errorPanel.classList.remove("d-none");
        const category = errData.category || "Runtime";
        elements.errorCategoryBadge.textContent = category;
        elements.errorMessageText.textContent = errData.message || "An unknown error occurred.";

        const line = errData.line || 0;
        const col = errData.col || 0;
        if (line > 0) {
            elements.errorLocationBadge.textContent = `Line ${line}, Col ${col}`;
            elements.errorLocationBadge.classList.remove("d-none");
            highlightErrorLine(line);
        } else {
            elements.errorLocationBadge.classList.add("d-none");
        }
    }

    function clearError() {
        elements.errorPanel.classList.add("d-none");
        clearErrorHighlight();
    }

    function renderOutput(data, mode) {
        currentResultData = data;
        elements.downloadCsvBtn.disabled = !data.columns || !data.rows;

        // Execution time
        const timeMs = data.time_ms !== undefined ? data.time_ms.toFixed(2) : "0.00";
        elements.execTimeBadge.innerHTML = `<i class="bi bi-stopwatch me-1"></i>${timeMs} ms`;

        // 1. Result Table
        if (data.columns && data.rows) {
            renderResultTable(data.columns, data.rows, data.row_count, timeMs);
        } else {
            elements.resultCountBadge.classList.add("d-none");
            elements.resultThead.innerHTML = `<tr><th class="text-muted">No rows returned for this query mode</th></tr>`;
            elements.resultTbody.innerHTML = `<tr><td class="text-muted text-center py-4">Output generated in stage tabs below.</td></tr>`;
        }

        // 2. Tokens Table
        if (data.tokens && Array.isArray(data.tokens)) {
            elements.tokensCountBadge.textContent = data.tokens.length;
            elements.tokensCountBadge.classList.remove("d-none");
            elements.tokensTbody.innerHTML = data.tokens.map(t => `
                <tr>
                    <td class="fw-semibold text-primary">${escapeHtml(t.type)}</td>
                    <td class="text-break">${escapeHtml(t.lexeme)}</td>
                    <td class="text-muted">${t.line}:${t.col}</td>
                </tr>
            `).join("");
        }

        // 3. AST
        if (data.ast) {
            elements.astPre.textContent = data.ast;
        }

        // 4. Logical Plan
        if (data.plan) {
            elements.planPre.textContent = data.plan;
        }

        // 5. Optimized Plan & Rules
        if (data.optimized_plan) {
            elements.optPlanPre.textContent = data.optimized_plan;
        }
        if (data.rules_fired && Array.isArray(data.rules_fired)) {
            elements.rulesCountBadge.textContent = data.rules_fired.length;
            elements.rulesCountBadge.classList.remove("d-none");
            if (data.rules_fired.length > 0) {
                elements.rulesFiredList.innerHTML = data.rules_fired.map(r =>
                    `<span class="badge bg-success-subtle text-success border border-success-subtle"><i class="bi bi-check2 me-1"></i>${escapeHtml(r)}</span>`
                ).join("");
            } else {
                elements.rulesFiredList.innerHTML = `<span class="badge bg-secondary-subtle text-muted">No rewrite rules applied</span>`;
            }
        }

        // 6. Compare Mode
        if (data.unoptimized && data.optimized) {
            renderCompare(data);
        }

        // Auto switch tab if appropriate
        if (mode === "compare" && data.unoptimized) {
            switchTab("tab-compare");
        } else if (mode === "tokens" && data.tokens) {
            switchTab("tab-tokens");
        } else if (mode === "ast" && data.ast) {
            switchTab("tab-ast");
        } else if ((mode === "plan" || mode === "explain") && data.plan) {
            switchTab("tab-plan");
        } else {
            switchTab("tab-result");
        }
    }

    function switchTab(tabId) {
        const btn = document.getElementById(tabId);
        if (btn && window.bootstrap && window.bootstrap.Tab) {
            const tab = bootstrap.Tab.getOrCreateInstance(btn);
            tab.show();
        }
    }

    function renderResultTable(columns, rows, totalCount, timeMs) {
        const count = totalCount !== undefined ? totalCount : rows.length;
        elements.resultCountBadge.textContent = count;
        elements.resultCountBadge.classList.remove("d-none");

        elements.resultMetaNotice.classList.remove("d-none");
        elements.resultStatusText.textContent = `Returned ${count} row${count === 1 ? "" : "s"} in ${timeMs} ms`;

        if (count > 500) {
            elements.resultTruncatedNotice.classList.remove("d-none");
            elements.resultTotalRows.textContent = count;
        } else {
            elements.resultTruncatedNotice.classList.add("d-none");
        }

        // Render Head
        elements.resultThead.innerHTML = `<tr>${columns.map(c => `<th>${escapeHtml(c)}</th>`).join("")}</tr>`;

        // Render Body (up to 500 rows)
        const displayRows = rows.slice(0, 500);
        if (displayRows.length === 0) {
            elements.resultTbody.innerHTML = `<tr><td colspan="${columns.length}" class="text-muted text-center py-4">0 rows returned.</td></tr>`;
            return;
        }

        elements.resultTbody.innerHTML = displayRows.map(row => `
            <tr>
                ${row.map(cell => {
                    if (cell === null || cell === undefined) {
                        return `<td><span class="badge-null">NULL</span></td>`;
                    }
                    return `<td>${escapeHtml(String(cell))}</td>`;
                }).join("")}
            </tr>
        `).join("");
    }

    function renderCompare(data) {
        const unopt = data.unoptimized;
        const opt = data.optimized;
        const redPct = data.reduction_pct !== undefined ? data.reduction_pct : 0.0;

        elements.reductionBadge.textContent = `${redPct.toFixed(1)}% Red.`;
        elements.reductionBadge.classList.remove("d-none");
        elements.compareReductionBadge.textContent = `${redPct.toFixed(1)}% Reduction in Rows Processed`;
        elements.compareReductionText.textContent = `Reduced rows processed from ${unopt.rows_processed} to ${opt.rows_processed}`;

        // Unoptimized
        elements.compareUnoptPlan.textContent = unopt.plan || "--";
        elements.unoptRowsProc.textContent = unopt.rows_processed !== undefined ? unopt.rows_processed : "--";
        elements.unoptTimeBadge.textContent = unopt.time_ms !== undefined ? `${unopt.time_ms.toFixed(2)} ms` : "--";
        if (unopt.stats && Array.isArray(unopt.stats)) {
            elements.compareUnoptStats.innerHTML = unopt.stats.map(s => `
                <tr><td>${escapeHtml(s.operator)}</td><td>${s.rows_in}</td><td>${s.rows_out}</td></tr>
            `).join("");
        }

        // Optimized
        elements.compareOptPlan.textContent = opt.plan || "--";
        elements.optRowsProc.textContent = opt.rows_processed !== undefined ? opt.rows_processed : "--";
        elements.optTimeBadge.textContent = opt.time_ms !== undefined ? `${opt.time_ms.toFixed(2)} ms` : "--";
        if (opt.rules_fired && Array.isArray(opt.rules_fired)) {
            elements.compareOptRules.innerHTML = opt.rules_fired.map(r =>
                `<span class="badge bg-success-subtle text-success border border-success-subtle small">${escapeHtml(r)}</span>`
            ).join("");
        }
        if (opt.stats && Array.isArray(opt.stats)) {
            elements.compareOptStats.innerHTML = opt.stats.map(s => `
                <tr><td>${escapeHtml(s.operator)}</td><td>${s.rows_in}</td><td>${s.rows_out}</td></tr>
            `).join("");
        }
    }

    // ── 6. CSV Download ───────────────────────────────────────────────
    function initDownloadCsv() {
        elements.downloadCsvBtn.addEventListener("click", () => {
            if (!currentResultData || !currentResultData.columns || !currentResultData.rows) return;
            const cols = currentResultData.columns;
            const rows = currentResultData.rows;

            let csvContent = cols.map(c => `"${c.replace(/"/g, '""')}"`).join(",") + "\n";
            rows.forEach(r => {
                const line = r.map(v => {
                    if (v === null || v === undefined) return "";
                    return `"${String(v).replace(/"/g, '""')}"`;
                }).join(",");
                csvContent += line + "\n";
            });

            const blob = new Blob([csvContent], { type: "text/csv;charset=utf-8;" });
            const url = URL.createObjectURL(blob);
            const a = document.createElement("a");
            a.href = url;
            a.download = `query_result_${Date.now()}.csv`;
            document.body.appendChild(a);
            a.click();
            document.body.removeChild(a);
            URL.revokeObjectURL(url);
        });
    }

    // ── 7. Tables Management ──────────────────────────────────────────
    async function loadTables() {
        elements.tablesSpinner.classList.remove("d-none");
        elements.noTablesMsg.classList.add("d-none");
        try {
            const resp = await fetch("/api/tables");
            const data = await resp.json();
            const tables = data.tables || [];
            elements.tableCountBadge.textContent = tables.length;

            if (tables.length === 0) {
                elements.noTablesMsg.classList.remove("d-none");
                elements.tablesAccordion.innerHTML = "";
                return;
            }

            renderTablesAccordion(tables);
        } catch (err) {
            console.error("Failed to load tables", err);
        } finally {
            elements.tablesSpinner.classList.add("d-none");
        }
    }

    function renderTablesAccordion(tables) {
        const query = elements.tableSearchInput.value.toLowerCase().trim();
        const filtered = query
            ? tables.filter(t => t.name.toLowerCase().includes(query))
            : tables;

        elements.tablesAccordion.innerHTML = filtered.map((t, idx) => `
            <div class="accordion-item border rounded mb-2 overflow-hidden table-item" data-table-name="${escapeHtml(t.name)}">
                <h2 class="accordion-header" id="heading-${idx}">
                    <button class="accordion-button collapsed py-2 px-2" type="button" data-bs-toggle="collapse" data-bs-target="#collapse-${idx}">
                        <div class="d-flex align-items-center justify-content-between w-100 me-2">
                            <span class="fw-semibold text-break">${escapeHtml(t.name)}</span>
                            <span class="badge bg-secondary-subtle text-body border small font-monospace">${t.row_count} rows</span>
                        </div>
                    </button>
                </h2>
                <div id="collapse-${idx}" class="accordion-collapse collapse" data-bs-parent="#tables-accordion">
                    <div class="accordion-body p-2">
                        <div class="mb-2">
                            <div class="text-muted small mb-1 fw-medium">Columns (${t.columns.length}):</div>
                            <div class="d-flex flex-wrap gap-1">
                                ${t.columns.map(c => `
                                    <span class="badge ${getTypeBadgeClass(c.type)} py-1 px-2 font-monospace small" title="${c.type}">
                                        ${escapeHtml(c.name)} <span class="opacity-75">(${c.type})</span>
                                    </span>
                                `).join("")}
                            </div>
                        </div>
                        <div class="d-flex justify-content-end gap-1 pt-1 border-top">
                            <button class="btn btn-xs btn-outline-primary py-0 px-2 btn-preview-table" data-table="${escapeHtml(t.name)}">
                                <i class="bi bi-eye"></i> Preview
                            </button>
                            <button class="btn btn-xs btn-outline-danger py-0 px-2 btn-delete-table" data-table="${escapeHtml(t.name)}">
                                <i class="bi bi-trash"></i> Delete
                            </button>
                        </div>
                    </div>
                </div>
            </div>
        `).join("");

        // Attach action handlers
        elements.tablesAccordion.querySelectorAll(".btn-preview-table").forEach(btn => {
            btn.addEventListener("click", () => previewTable(btn.getAttribute("data-table")));
        });

        elements.tablesAccordion.querySelectorAll(".btn-delete-table").forEach(btn => {
            btn.addEventListener("click", () => confirmDeleteTable(btn.getAttribute("data-table")));
        });
    }

    function getTypeBadgeClass(type) {
        switch ((type || "").toUpperCase()) {
            case "INT": return "badge-type-int";
            case "FLOAT": return "badge-type-float";
            case "STRING": return "badge-type-string";
            case "BOOL": return "badge-type-bool";
            default: return "bg-secondary text-white";
        }
    }

    async function previewTable(tableName) {
        try {
            const resp = await fetch(`/api/tables/${encodeURIComponent(tableName)}`);
            if (!resp.ok) {
                const err = await resp.json();
                alert(`Error previewing table: ${err.message}`);
                return;
            }
            const data = await resp.json();

            elements.previewModalTitle.textContent = `Table: ${data.name}`;
            elements.previewModalRowCount.textContent = `${data.row_count} total rows`;
            elements.previewShowingNotice.textContent = `Showing first ${data.preview_count} rows`;

            elements.previewSchemaBadges.innerHTML = data.columns.map(c => `
                <span class="badge ${getTypeBadgeClass(c.type)} px-2 py-1 font-monospace">
                    ${escapeHtml(c.name)}: <strong>${c.type}</strong>
                </span>
            `).join("");

            elements.previewTableThead.innerHTML = `<tr>${data.columns.map(c => `<th>${escapeHtml(c.name)}</th>`).join("")}</tr>`;

            elements.previewTableTbody.innerHTML = data.rows.map(row => `
                <tr>
                    ${row.map(val => {
                        if (val === null || val === "" || String(val).toLowerCase() === "null") {
                            return `<td><span class="badge-null">NULL</span></td>`;
                        }
                        return `<td>${escapeHtml(String(val))}</td>`;
                    }).join("")}
                </tr>
            `).join("");

            const modal = new bootstrap.Modal(elements.modalTablePreview);
            modal.show();
        } catch (err) {
            alert(`Failed to preview table: ${err.message}`);
        }
    }

    async function confirmDeleteTable(tableName) {
        if (!confirm(`Are you sure you want to delete table '${tableName}'? This cannot be undone.`)) {
            return;
        }
        try {
            const resp = await fetch(`/api/tables/${encodeURIComponent(tableName)}`, {
                method: "DELETE"
            });
            const data = await resp.json();
            if (!resp.ok) {
                alert(`Failed to delete table: ${data.message}`);
            } else {
                await loadTables();
            }
        } catch (err) {
            alert(`Error deleting table: ${err.message}`);
        }
    }

    // ── 8. Upload CSV Form ────────────────────────────────────────────
    function initUploadForm() {
        elements.formUploadCsv.addEventListener("submit", async (e) => {
            e.preventDefault();
            const name = elements.uploadTableName.value.trim();
            const file = elements.uploadCsvFile.files[0];

            if (!name.match(/^[A-Za-z_][A-Za-z0-9_]*$/)) {
                showUploadError("Table name must match ^[A-Za-z_][A-Za-z0-9_]*$");
                return;
            }
            if (!file) {
                showUploadError("Please select a .csv file.");
                return;
            }
            if (!file.name.toLowerCase().endsWith(".csv")) {
                showUploadError("Only .csv files are supported.");
                return;
            }
            if (file.size > 5 * 1024 * 1024) {
                showUploadError("File size exceeds 5 MB limit.");
                return;
            }

            elements.uploadErrorAlert.classList.add("d-none");
            elements.uploadSpinner.classList.remove("d-none");
            elements.btnSubmitUpload.disabled = true;

            const formData = new FormData();
            formData.append("name", name);
            formData.append("file", file);

            try {
                const resp = await fetch("/api/tables/upload", {
                    method: "POST",
                    body: formData
                });
                const data = await resp.json();
                if (!resp.ok || data.status === "error") {
                    showUploadError(data.message || "Failed to upload table");
                } else {
                    // Close modal and refresh
                    bootstrap.Modal.getInstance(document.getElementById("modal-add-table")).hide();
                    elements.formUploadCsv.reset();
                    await loadTables();
                }
            } catch (err) {
                showUploadError(`Network error: ${err.message}`);
            } finally {
                elements.uploadSpinner.classList.add("d-none");
                elements.btnSubmitUpload.disabled = false;
            }
        });
    }

    function showUploadError(msg) {
        elements.uploadErrorAlert.textContent = msg;
        elements.uploadErrorAlert.classList.remove("d-none");
    }

    // ── 9. Manual Table Builder ───────────────────────────────────────
    function initManualTableBuilder() {
        // Add initial 2 column rows
        addColumnRow("id", "INT");
        addColumnRow("name", "STRING");
        updateManualPreview();

        elements.btnAddColRow.addEventListener("click", () => {
            const count = elements.columnBuilderContainer.querySelectorAll(".col-def-row").length;
            if (count >= 20) {
                alert("Maximum of 20 columns allowed.");
                return;
            }
            addColumnRow(`col_${count + 1}`, "STRING");
            updateManualPreview();
        });

        elements.manualRowsTextarea.addEventListener("input", () => {
            updateManualPreview();
        });

        elements.formCreateManual.addEventListener("submit", async (e) => {
            e.preventDefault();
            const name = elements.manualTableName.value.trim();

            if (!name.match(/^[A-Za-z_][A-Za-z0-9_]*$/)) {
                showManualError("Table name must match ^[A-Za-z_][A-Za-z0-9_]*$");
                return;
            }

            const colRows = elements.columnBuilderContainer.querySelectorAll(".col-def-row");
            const columns = [];
            const seen = new Set();
            for (const r of colRows) {
                const cname = r.querySelector(".col-name-input").value.trim();
                const ctype = r.querySelector(".col-type-select").value;
                if (!cname.match(/^[A-Za-z_][A-Za-z0-9_]*$/)) {
                    showManualError(`Invalid column name '${cname}'. Must match ^[A-Za-z_][A-Za-z0-9_]*$`);
                    return;
                }
                if (seen.has(cname.toLowerCase())) {
                    showManualError(`Duplicate column name '${cname}'`);
                    return;
                }
                seen.add(cname.toLowerCase());
                columns.push({ name: cname, type: ctype });
            }

            // Parse rows textarea
            const rawText = elements.manualRowsTextarea.value.trim();
            const rows = [];
            if (rawText) {
                const lines = rawText.split("\n");
                for (let i = 0; i < lines.length; i++) {
                    const line = lines[i].trim();
                    if (!line) continue;
                    const parts = line.split(",").map(p => p.trim());
                    if (parts.length !== columns.length) {
                        showManualError(`Row ${i + 1} has ${parts.length} values, but there are ${columns.length} columns defined.`);
                        return;
                    }
                    rows.push(parts);
                }
            }

            elements.manualErrorAlert.classList.add("d-none");
            elements.manualSpinner.classList.remove("d-none");
            elements.btnSubmitManual.disabled = true;

            try {
                const resp = await fetch("/api/tables/create", {
                    method: "POST",
                    headers: { "Content-Type": "application/json" },
                    body: JSON.stringify({ name, columns, rows })
                });
                const data = await resp.json();
                if (!resp.ok || data.status === "error") {
                    showManualError(data.message || "Failed to create table");
                } else {
                    bootstrap.Modal.getInstance(document.getElementById("modal-add-table")).hide();
                    elements.formCreateManual.reset();
                    await loadTables();
                }
            } catch (err) {
                showManualError(`Network error: ${err.message}`);
            } finally {
                elements.manualSpinner.classList.add("d-none");
                elements.btnSubmitManual.disabled = false;
            }
        });
    }

    function addColumnRow(nameVal = "", typeVal = "STRING") {
        const row = document.createElement("div");
        row.className = "row g-2 align-items-center mb-2 col-def-row";
        row.innerHTML = `
            <div class="col-7">
                <input type="text" class="form-control form-control-sm font-monospace col-name-input" placeholder="column_name" value="${escapeHtml(nameVal)}" required>
            </div>
            <div class="col-3">
                <select class="form-select form-select-sm col-type-select">
                    <option value="INT" ${typeVal === "INT" ? "selected" : ""}>INT</option>
                    <option value="FLOAT" ${typeVal === "FLOAT" ? "selected" : ""}>FLOAT</option>
                    <option value="STRING" ${typeVal === "STRING" ? "selected" : ""}>STRING</option>
                    <option value="BOOL" ${typeVal === "BOOL" ? "selected" : ""}>BOOL</option>
                </select>
            </div>
            <div class="col-2 text-end">
                <button type="button" class="btn btn-sm btn-outline-danger btn-remove-col py-0 px-2" title="Remove column">
                    <i class="bi bi-x-lg"></i>
                </button>
            </div>
        `;

        row.querySelector(".col-name-input").addEventListener("input", updateManualPreview);
        row.querySelector(".col-type-select").addEventListener("change", updateManualPreview);
        row.querySelector(".btn-remove-col").addEventListener("click", () => {
            const count = elements.columnBuilderContainer.querySelectorAll(".col-def-row").length;
            if (count <= 1) {
                alert("At least one column is required.");
                return;
            }
            row.remove();
            updateManualPreview();
        });

        elements.columnBuilderContainer.appendChild(row);
    }

    function updateManualPreview() {
        const colRows = elements.columnBuilderContainer.querySelectorAll(".col-def-row");
        const cols = [];
        colRows.forEach(r => {
            const name = r.querySelector(".col-name-input").value.trim() || "col";
            const type = r.querySelector(".col-type-select").value;
            cols.push({ name, type });
        });

        elements.manualPreviewThead.innerHTML = cols.map(c =>
            `<th>${escapeHtml(c.name)} <span class="text-muted fw-normal">(${c.type})</span></th>`
        ).join("");

        const rawText = elements.manualRowsTextarea.value.trim();
        if (!rawText) {
            elements.manualPreviewTbody.innerHTML = `<tr><td class="text-muted text-center py-2" colspan="${cols.length}">Enter comma-separated rows above.</td></tr>`;
            return;
        }

        const lines = rawText.split("\n").filter(l => l.trim().length > 0);
        elements.manualPreviewTbody.innerHTML = lines.slice(0, 5).map(line => {
            const parts = line.split(",").map(p => p.trim());
            return `<tr>${cols.map((_, i) => `<td>${escapeHtml(parts[i] !== undefined ? parts[i] : "")}</td>`).join("")}</tr>`;
        }).join("");
    }

    function showManualError(msg) {
        elements.manualErrorAlert.textContent = msg;
        elements.manualErrorAlert.classList.remove("d-none");
    }

    // ── 10. Workspace Reset ───────────────────────────────────────────
    function initResetWorkspace() {
        const handler = async () => {
            if (!confirm("Reset workspace to initial sample CSV tables? All custom tables will be deleted.")) {
                return;
            }
            try {
                const resp = await fetch("/api/reset", { method: "POST" });
                const data = await resp.json();
                if (resp.ok) {
                    await loadTables();
                    alert("Workspace successfully reset to sample tables.");
                } else {
                    alert(`Failed to reset workspace: ${data.message}`);
                }
            } catch (err) {
                alert(`Error resetting workspace: ${err.message}`);
            }
        };

        elements.resetWsNavBtn.addEventListener("click", handler);
        elements.resetWsSidebarBtn.addEventListener("click", handler);
    }

    // ── 11. Clipboard Helper ──────────────────────────────────────────
    function initCopyButtons() {
        const setup = (btnId, preId) => {
            const btn = document.getElementById(btnId);
            const pre = document.getElementById(preId);
            if (btn && pre) {
                btn.addEventListener("click", () => {
                    navigator.clipboard.writeText(pre.textContent).then(() => {
                        const original = btn.innerHTML;
                        btn.innerHTML = `<i class="bi bi-check2"></i> Copied!`;
                        setTimeout(() => { btn.innerHTML = original; }, 1500);
                    });
                });
            }
        };

        setup("btn-copy-ast", "ast-pre");
        setup("btn-copy-plan", "plan-pre");
        setup("btn-copy-opt-plan", "opt-plan-pre");
    }

    // Utility: HTML Escaping
    function escapeHtml(str) {
        if (str === null || str === undefined) return "";
        return String(str)
            .replace(/&/g, "&amp;")
            .replace(/</g, "&lt;")
            .replace(/>/g, "&gt;")
            .replace(/"/g, "&quot;")
            .replace(/'/g, "&#039;");
    }

    // ── Initialize App on DOM Ready ───────────────────────────────────
    document.addEventListener("DOMContentLoaded", () => {
        initTheme();
        initEditor();
        renderHistory();
        loadExamples();
        loadTables();
        initUploadForm();
        initManualTableBuilder();
        initResetWorkspace();
        initDownloadCsv();
        initCopyButtons();

        // Run Button Listener
        elements.runBtn.addEventListener("click", runQuery);

        // Filter tables live input
        elements.tableSearchInput.addEventListener("input", () => {
            loadTables();
        });
        elements.refreshTablesBtn.addEventListener("click", loadTables);
    });
})();
