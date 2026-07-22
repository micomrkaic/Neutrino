;;; Batch tests for neutrino-mode. Run: emacs --batch -l tests/run_emacs.el
(setq debug-on-error t)
(load (expand-file-name "editors/neutrino-mode.el") nil t)

(defun nu-fail (msg) (message "emacs-mode FAIL: %s" msg) (kill-emacs 1))

;; ---- fontification ----
(with-temp-buffer
  (insert "let cube = fn x -> x^3   % a comment\n"
          "let v = [1, 2, 3]'\n"
          "sqrt(sum(v .* v))\n"
          "let s = \"a % not comment | pipe\"\n")
  (neutrino-mode)
  (font-lock-ensure)
  (defun nu--face-at (str)
    (get-text-property (save-excursion (goto-char (point-min))
                                       (search-forward str)
                                       (match-beginning 0))
                       'face))
  (progn
    (unless (eq (nu--face-at "let") 'font-lock-keyword-face) (nu-fail "let not keyword"))
    (unless (eq (nu--face-at "cube") 'font-lock-function-name-face) (nu-fail "fn name"))
    (unless (eq (nu--face-at "v = [1") 'font-lock-variable-name-face) (nu-fail "var name"))
    (unless (eq (nu--face-at "sqrt") 'font-lock-builtin-face) (nu-fail "builtin"))
    (unless (memq (nu--face-at "% a comment") '(font-lock-comment-face font-lock-comment-delimiter-face)) (nu-fail "comment delimiter"))
    (unless (eq (nu--face-at "a comment") 'font-lock-comment-face) (nu-fail "comment body"))
    (unless (eq (nu--face-at "a % not") 'font-lock-string-face) (nu-fail "string body"))
    ;; the transpose quote must NOT open a string: sqrt on line 3 already
    ;; proved fontification continues; also check the char after ' is not string
    (save-excursion (goto-char (point-min)) (search-forward "]'")
                    (when (nth 3 (syntax-ppss (point))) (nu-fail "transpose opened a string")))))
(message "emacs-mode: fontification OK")

;; ---- indentation golden ----
(with-temp-buffer
  (insert "let f = fn x -> (\n"
          "let y = x + 1;\n"
          "for i = 1:3 do\n"
          "y = y * 2\n"
          "end;\n"
          "y)\n")
  (neutrino-mode)
  (indent-region (point-min) (point-max))
  (let ((want (concat "let f = fn x -> (\n"
                      "  let y = x + 1;\n"
                      "  for i = 1:3 do\n"
                      "    y = y * 2\n"
                      "  end;\n"
                      "  y)\n")))
    (unless (string= (buffer-string) want)
      (message "got:\n%s" (buffer-string))
      (nu-fail "indentation golden mismatch"))))
(message "emacs-mode: indentation OK")

;; ---- inferior REPL against the real interpreter ----
(when (file-executable-p "./neutrino")
  (setq neutrino-program (expand-file-name "./neutrino"))
  (run-neutrino)
  (neutrino--send "6 * 7")
  (with-current-buffer neutrino-repl-buffer
    (let ((deadline (+ (float-time) 5)) (ok nil))
      (while (and (not ok) (< (float-time) deadline))
        (accept-process-output (get-buffer-process (current-buffer)) 0.2)
        (setq ok (string-match-p "42" (buffer-string))))
      (unless ok (nu-fail "REPL did not answer 42"))))
  (message "emacs-mode: inferior REPL OK"))

(message "emacs-mode: all checks passed")
(kill-emacs 0)
