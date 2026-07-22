;;; neutrino-mode.el --- Major mode for the Neutrino language -*- lexical-binding: t; -*-

;; Author: Mico Mrkaic and Claude
;; Keywords: languages
;; URL: https://github.com/micomrkaic/Neutrino
;; Package-Requires: ((emacs "26.1"))
;; SPDX-License-Identifier: GPL-3.0-or-later

;;; Commentary:
;; Editing support for Neutrino (.nu): syntax highlighting, block-aware
;; indentation, and an inferior REPL.
;;
;; Install: put this file on your `load-path' and add to your init file:
;;   (require 'neutrino-mode)
;; Files ending in .nu open in `neutrino-mode' automatically.
;;
;; REPL: M-x run-neutrino, then from a .nu buffer:
;;   C-c C-r  send region      C-c C-b  send buffer
;;   C-c C-l  load this file   C-c C-z  switch to the REPL
;;
;; The builtin-name list below is generated from the interpreter's own
;; documentation table by tools/gen_emacs_mode.py, so highlighting cannot
;; drift from the language.

;;; Code:

(require 'comint)

(defgroup neutrino nil
  "Editing Neutrino code."
  :group 'languages)

(defcustom neutrino-indent-offset 2
  "Indentation per block or bracket level in Neutrino."
  :type 'integer :group 'neutrino)

(defcustom neutrino-program "neutrino"
  "Program name for the inferior Neutrino REPL."
  :type 'string :group 'neutrino)

;; ---------------------------------------------------------------------
;; Syntax
;; ---------------------------------------------------------------------
(defvar neutrino-mode-syntax-table
  (let ((st (make-syntax-table)))
    (modify-syntax-entry ?% "<" st)     ; % comments to end of line
    (modify-syntax-entry ?\n ">" st)
    (modify-syntax-entry ?\" "\"" st)   ; double-quoted strings
    (modify-syntax-entry ?\\ "\\" st)
    (modify-syntax-entry ?' "." st)     ; transpose operator, NOT a quote
    (modify-syntax-entry ?_ "_" st)
    st)
  "Syntax table for `neutrino-mode'.")

(defconst neutrino--keywords
  '("let" "fn" "if" "then" "else" "end" "for" "while" "do" "in"
    "return" "break" "continue")
  "Neutrino reserved words.")

(defconst neutrino--constants
  '("true" "false" "null" "ans")
  "Neutrino literal words and the REPL's `ans'.")

(defconst neutrino--builtins
  '("abs"
    "acos"
    "acosh"
    "all"
    "angle"
    "any"
    "arg"
    "asin"
    "asinh"
    "assert"
    "atan"
    "atan2"
    "atanh"
    "besselj"
    "bessely"
    "beta"
    "betainc"
    "body"
    "cbrt"
    "ceil"
    "chol"
    "clear"
    "conj"
    "contains"
    "corr"
    "cos"
    "cosh"
    "cov"
    "cumprod"
    "cumsum"
    "det"
    "diag"
    "diff"
    "digamma"
    "dis"
    "dot"
    "eig"
    "endswith"
    "erf"
    "erfc"
    "error"
    "exit"
    "exp"
    "eye"
    "fields"
    "find"
    "fliplr"
    "flipud"
    "floor"
    "fminbnd"
    "fmt"
    "format"
    "fzero"
    "gamma"
    "gammainc"
    "help"
    "hist"
    "hypot"
    "imag"
    "integral"
    "inv"
    "isfinite"
    "isinf"
    "isnan"
    "kron"
    "lbeta"
    "length"
    "lgamma"
    "linspace"
    "ln"
    "load"
    "log"
    "log10"
    "log2"
    "lower"
    "lu"
    "manual"
    "map"
    "max"
    "mean"
    "median"
    "mem"
    "min"
    "mod"
    "more"
    "norm"
    "norminv"
    "now"
    "num"
    "numel"
    "ones"
    "plot"
    "pretty"
    "print"
    "prod"
    "qr"
    "quantile"
    "rand"
    "randi"
    "randn"
    "readcsv"
    "readtable"
    "real"
    "rem"
    "repmat"
    "reshape"
    "rng"
    "round"
    "save"
    "sign"
    "sin"
    "sinh"
    "size"
    "sort"
    "sqrt"
    "startswith"
    "std"
    "str"
    "strjoin"
    "strrep"
    "strsplit"
    "sum"
    "svd"
    "system"
    "tan"
    "tanh"
    "tic"
    "toc"
    "trace"
    "trim"
    "trunc"
    "unique"
    "upper"
    "var"
    "version"
    "where"
    "who"
    "whof"
    "whor"
    "whos"
    "whov"
    "writecsv"
    "y"
    "zeros")
  "Builtin names, generated from eval.c — do not edit by hand.")

(defvar neutrino-font-lock-keywords
  `((,(concat "\\_<let[ \t]+\\([A-Za-z_][A-Za-z0-9_]*\\)[ \t]*=[ \t]*fn\\_>")
     (1 font-lock-function-name-face))
    (,(concat "\\_<let[ \t]+\\([A-Za-z_][A-Za-z0-9_]*\\)")
     (1 font-lock-variable-name-face))
    (,(regexp-opt neutrino--keywords 'symbols) . font-lock-keyword-face)
    (,(regexp-opt neutrino--constants 'symbols) . font-lock-constant-face)
    (,(regexp-opt neutrino--builtins 'symbols) . font-lock-builtin-face))
  "Font-lock rules for `neutrino-mode'.")

;; ---------------------------------------------------------------------
;; Indentation: bracket depth (via the parser state) plus block keywords
;; (if/for/while ... end). fn bodies are parenthesized in idiomatic
;; Neutrino, so bracket depth carries most of the weight.
;; ---------------------------------------------------------------------
(defconst neutrino--block-open-re
  "\\_<\\(if\\|for\\|while\\)\\_>")
(defconst neutrino--block-close-re
  "\\_<end\\_>")

(defun neutrino--block-depth-before (pos)
  "Block-keyword depth (opens minus ends) in code from buffer start to POS."
  (save-excursion
    (goto-char (point-min))
    (let ((depth 0))
      (while (re-search-forward
              "\\_<\\(?:if\\|for\\|while\\|end\\)\\_>" pos t)
        (let ((state (syntax-ppss)))
          (unless (or (nth 3 state) (nth 4 state))  ; not in string/comment
            (if (string= (match-string 0) "end")
                (setq depth (max 0 (1- depth)))
              (setq depth (1+ depth))))))
      depth)))

(defun neutrino-indent-line ()
  "Indent the current line of Neutrino code."
  (interactive)
  (let* ((bol (save-excursion (beginning-of-line) (point)))
         (paren-depth (car (syntax-ppss bol)))
         (block-depth (neutrino--block-depth-before bol))
         (dedent (save-excursion
                   (beginning-of-line)
                   (skip-chars-forward " \t")
                   (if (looking-at "\\_<\\(end\\|else\\)\\_>\\|[])}]") 1 0)))
         (target (* neutrino-indent-offset
                    (max 0 (- (+ paren-depth block-depth) dedent)))))
    (if (<= (current-column) (current-indentation))
        (indent-line-to target)
      (save-excursion (indent-line-to target)))))

;; ---------------------------------------------------------------------
;; Inferior REPL
;; ---------------------------------------------------------------------
(defvar neutrino-repl-buffer "*neutrino*")

;;;###autoload
(defun run-neutrino ()
  "Start (or switch to) an inferior Neutrino REPL."
  (interactive)
  (unless (comint-check-proc neutrino-repl-buffer)
    (with-current-buffer (make-comint "neutrino" neutrino-program)
      (setq-local comint-prompt-regexp "^neutrino> *")
      (setq-local comint-prompt-read-only t)))
  (pop-to-buffer neutrino-repl-buffer))

(defun neutrino--send (text)
  (run-neutrino)
  (with-current-buffer neutrino-repl-buffer
    (comint-send-string (get-buffer-process (current-buffer))
                        (concat text "\n"))))

(defun neutrino-send-region (beg end)
  "Send the region to the Neutrino REPL."
  (interactive "r")
  (neutrino--send (buffer-substring-no-properties beg end)))

(defun neutrino-send-buffer ()
  "Send the whole buffer to the Neutrino REPL."
  (interactive)
  (neutrino-send-region (point-min) (point-max)))

(defun neutrino-load-file ()
  "Load the current file in the Neutrino REPL (saves first)."
  (interactive)
  (save-buffer)
  (neutrino--send (format "load(%S)" (buffer-file-name))))

(defun neutrino-switch-to-repl ()
  "Switch to the Neutrino REPL buffer."
  (interactive)
  (run-neutrino))

;; ---------------------------------------------------------------------
;; The mode
;; ---------------------------------------------------------------------
(defvar neutrino-mode-map
  (let ((map (make-sparse-keymap)))
    (define-key map (kbd "C-c C-r") #'neutrino-send-region)
    (define-key map (kbd "C-c C-b") #'neutrino-send-buffer)
    (define-key map (kbd "C-c C-l") #'neutrino-load-file)
    (define-key map (kbd "C-c C-z") #'neutrino-switch-to-repl)
    map)
  "Keymap for `neutrino-mode'.")

;;;###autoload
(define-derived-mode neutrino-mode prog-mode "Neutrino"
  "Major mode for editing Neutrino code."
  :syntax-table neutrino-mode-syntax-table
  (setq-local comment-start "% ")
  (setq-local comment-start-skip "%+[ \t]*")
  (setq-local font-lock-defaults '(neutrino-font-lock-keywords))
  (setq-local indent-line-function #'neutrino-indent-line))

;;;###autoload
(add-to-list 'auto-mode-alist '("\\.nu\\'" . neutrino-mode))

(provide 'neutrino-mode)
;;; neutrino-mode.el ends here
