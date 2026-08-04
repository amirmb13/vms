/**
 * VMS Admin Theme — small runtime fixes.
 *
 * Django's fa locale is missing translations for the newer SelectFilter2
 * (filter_horizontal) strings, so the widget renders mixed English/Farsi
 * labels ("Choose all اجازه‌ها"). Replace them with clean Farsi labels
 * after the widget is built.
 */
(function () {
  "use strict";

  function localizeSelectors(root) {
    root.querySelectorAll("button.selector-chooseall").forEach(function (btn) {
      btn.textContent = "انتخاب همه";
      btn.setAttribute("title", "انتخاب همهٔ موارد");
    });
    root.querySelectorAll("button.selector-clearall").forEach(function (btn) {
      btn.textContent = "حذف همه";
      btn.setAttribute("title", "حذف همهٔ موارد");
    });
    root.querySelectorAll("button.selector-add").forEach(function (btn) {
      btn.setAttribute("aria-label", "افزودن موارد انتخاب‌شده");
      btn.setAttribute("title", "افزودن موارد انتخاب‌شده");
    });
    root.querySelectorAll("button.selector-remove").forEach(function (btn) {
      btn.setAttribute("aria-label", "حذف موارد انتخاب‌شده");
      btn.setAttribute("title", "حذف موارد انتخاب‌شده");
    });
    root.querySelectorAll(".selector .selector-filter input").forEach(function (input) {
      input.setAttribute("placeholder", "جستجو…");
    });
  }

  function init() {
    localizeSelectors(document);

    // Inline formsets ("add another") can build new selectors later.
    var observer = new MutationObserver(function (mutations) {
      for (var i = 0; i < mutations.length; i++) {
        for (var j = 0; j < mutations[i].addedNodes.length; j++) {
          var node = mutations[i].addedNodes[j];
          if (node.nodeType === 1 && (node.classList.contains("selector") || node.querySelector(".selector"))) {
            localizeSelectors(node);
          }
        }
      }
    });
    observer.observe(document.body, { childList: true, subtree: true });
  }

  // SelectFilter2 builds the widget on DOMContentLoaded; run after it.
  if (document.readyState === "complete") {
    init();
  } else {
    window.addEventListener("load", init);
  }
})();
