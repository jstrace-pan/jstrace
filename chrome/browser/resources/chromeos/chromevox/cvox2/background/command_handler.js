// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview ChromeVox commands.
 */

goog.provide('CommandHandler');

goog.require('ChromeVoxState');
goog.require('Output');
goog.require('cvox.ChromeVoxBackground');

goog.scope(function() {
var AutomationNode = chrome.automation.AutomationNode;
var Dir = constants.Dir;
var EventType = chrome.automation.EventType;
var RoleType = chrome.automation.RoleType;

/**
 * Handles ChromeVox Next commands.
 * @param {string} command
 * @return {boolean} True if the command should propagate.
 */
CommandHandler.onCommand = function(command) {
  // Check for loss of focus which results in us invalidating our current
  // range. Note this call is synchronis.
  chrome.automation.getFocus(function(focusedNode) {
    var cur = ChromeVoxState.instance.currentRange;
    if (cur && !cur.isValid()) {
      ChromeVoxState.instance.setCurrentRange(
          cursors.Range.fromNode(focusedNode));
    }

    if (!focusedNode)
      ChromeVoxState.instance.setCurrentRange(null);
  });

  // These commands don't require a current range and work in all modes.
  switch (command) {
    case 'speakTimeAndDate':
      chrome.automation.getDesktop(function(d) {
        // First, try speaking the on-screen time.
        var allTime = d.findAll({role: RoleType.time});
        allTime.filter(function(t) { return t.root.role == RoleType.desktop; });

        var timeString = '';
        allTime.forEach(function(t) {
          if (t.name) timeString = t.name;
        });
        if (timeString) {
          cvox.ChromeVox.tts.speak(timeString, cvox.QueueMode.FLUSH);
        } else {
          // Fallback to the old way of speaking time.
          var output = new Output();
          var dateTime = new Date();
          output
              .withString(
                  dateTime.toLocaleTimeString() + ', ' +
                  dateTime.toLocaleDateString())
              .go();
        }
      });
      return false;
    case 'showOptionsPage':
      chrome.runtime.openOptionsPage();
      break;
    case 'toggleChromeVox':
      if (cvox.ChromeVox.isChromeOS)
        return false;

      cvox.ChromeVox.isActive = !cvox.ChromeVox.isActive;
      if (!cvox.ChromeVox.isActive) {
        var msg = Msgs.getMsg('chromevox_inactive');
        cvox.ChromeVox.tts.speak(msg, cvox.QueueMode.FLUSH);
        return false;
      }
      break;
    case 'toggleStickyMode':
      cvox.ChromeVoxBackground.setPref(
          'sticky', !cvox.ChromeVox.isStickyPrefOn, true);

      if (cvox.ChromeVox.isStickyPrefOn)
        chrome.accessibilityPrivate.setKeyboardListener(true, true);
      else
        chrome.accessibilityPrivate.setKeyboardListener(true, false);
      return false;
    case 'passThroughMode':
      cvox.ChromeVox.passThroughMode = true;
      cvox.ChromeVox.tts.speak(
          Msgs.getMsg('pass_through_key'), cvox.QueueMode.QUEUE);
      return true;
    case 'showKbExplorerPage':
      var explorerPage = {url: 'chromevox/background/kbexplorer.html'};
      chrome.tabs.create(explorerPage);
      break;
    case 'decreaseTtsRate':
      CommandHandler.increaseOrDecreaseSpeechProperty_(
          cvox.AbstractTts.RATE, false);
      return false;
    case 'increaseTtsRate':
      CommandHandler.increaseOrDecreaseSpeechProperty_(
          cvox.AbstractTts.RATE, true);
      return false;
    case 'decreaseTtsPitch':
      CommandHandler.increaseOrDecreaseSpeechProperty_(
          cvox.AbstractTts.PITCH, false);
      return false;
    case 'increaseTtsPitch':
      CommandHandler.increaseOrDecreaseSpeechProperty_(
          cvox.AbstractTts.PITCH, true);
      return false;
    case 'decreaseTtsVolume':
      CommandHandler.increaseOrDecreaseSpeechProperty_(
          cvox.AbstractTts.VOLUME, false);
      return false;
    case 'increaseTtsVolume':
      CommandHandler.increaseOrDecreaseSpeechProperty_(
          cvox.AbstractTts.VOLUME, true);
      return false;
    case 'stopSpeech':
      cvox.ChromeVox.tts.stop();
      ChromeVoxState.isReadingContinuously = false;
      return false;
    case 'toggleEarcons':
      cvox.AbstractEarcons.enabled = !cvox.AbstractEarcons.enabled;
      var announce = cvox.AbstractEarcons.enabled ? Msgs.getMsg('earcons_on') :
                                                    Msgs.getMsg('earcons_off');
      cvox.ChromeVox.tts.speak(
          announce, cvox.QueueMode.FLUSH,
          cvox.AbstractTts.PERSONALITY_ANNOTATION);
      return false;
    case 'cycleTypingEcho':
      cvox.ChromeVox.typingEcho =
          cvox.TypingEcho.cycle(cvox.ChromeVox.typingEcho);
      var announce = '';
      switch (cvox.ChromeVox.typingEcho) {
        case cvox.TypingEcho.CHARACTER:
          announce = Msgs.getMsg('character_echo');
          break;
        case cvox.TypingEcho.WORD:
          announce = Msgs.getMsg('word_echo');
          break;
        case cvox.TypingEcho.CHARACTER_AND_WORD:
          announce = Msgs.getMsg('character_and_word_echo');
          break;
        case cvox.TypingEcho.NONE:
          announce = Msgs.getMsg('none_echo');
          break;
      }
      cvox.ChromeVox.tts.speak(
          announce, cvox.QueueMode.FLUSH,
          cvox.AbstractTts.PERSONALITY_ANNOTATION);
      return false;
    case 'cyclePunctuationEcho':
      cvox.ChromeVox.tts.speak(
          Msgs.getMsg(ChromeVoxState.backgroundTts.cyclePunctuationEcho()),
          cvox.QueueMode.FLUSH);
      return false;
    case 'reportIssue':
      var url = Background.ISSUE_URL;
      var description = {};
      description['Mode'] = ChromeVoxState.instance.mode;
      description['Version'] = chrome.app.getDetails().version;
      description['Reproduction Steps'] = '%0a1.%0a2.%0a3.';
      for (var key in description)
        url += key + ':%20' + description[key] + '%0a';
      chrome.tabs.create({url: url});
      return false;
    case 'toggleBrailleCaptions':
      cvox.BrailleCaptionsBackground.setActive(
          !cvox.BrailleCaptionsBackground.isEnabled());
      return false;
    case 'toggleChromeVoxVersion':
      if (!ChromeVoxState.instance.toggleNext())
        return false;
      if (ChromeVoxState.instance.currentRange) {
        ChromeVoxState.instance.navigateToRange(
            ChromeVoxState.instance.currentRange);
      }
      break;
    case 'showNextUpdatePage':
      (new PanelCommand(PanelCommandType.TUTORIAL)).send();
      return false;
    default:
      break;
  }

  // Require a current range.
  if (!ChromeVoxState.instance.currentRange_)
    return true;

  // Next/compat commands hereafter.
  if (ChromeVoxState.instance.mode == ChromeVoxMode.CLASSIC) return true;

  var current = ChromeVoxState.instance.currentRange_;
  var dir = Dir.FORWARD;
  var pred = null;
  var predErrorMsg = undefined;
  var rootPred = AutomationPredicate.root;
  var speechProps = {};
  switch (command) {
    case 'nextCharacter':
      speechProps['phoneticCharacters'] = true;
      current = current.move(cursors.Unit.CHARACTER, Dir.FORWARD);
      break;
    case 'previousCharacter':
      speechProps['phoneticCharacters'] = true;
      current = current.move(cursors.Unit.CHARACTER, Dir.BACKWARD);
      break;
    case 'nextWord':
      current = current.move(cursors.Unit.WORD, Dir.FORWARD);
      break;
    case 'previousWord':
      current = current.move(cursors.Unit.WORD, Dir.BACKWARD);
      break;
    case 'forward':
    case 'nextLine':
      current = current.move(cursors.Unit.LINE, Dir.FORWARD);
      break;
    case 'backward':
    case 'previousLine':
      current = current.move(cursors.Unit.LINE, Dir.BACKWARD);
      break;
    case 'nextButton':
      dir = Dir.FORWARD;
      pred = AutomationPredicate.button;
      predErrorMsg = 'no_next_button';
      break;
    case 'previousButton':
      dir = Dir.BACKWARD;
      pred = AutomationPredicate.button;
      predErrorMsg = 'no_previous_button';
      break;
    case 'nextCheckbox':
      dir = Dir.FORWARD;
      pred = AutomationPredicate.checkBox;
      predErrorMsg = 'no_next_checkbox';
      break;
    case 'previousCheckbox':
      dir = Dir.BACKWARD;
      pred = AutomationPredicate.checkBox;
      predErrorMsg = 'no_previous_checkbox';
      break;
    case 'nextComboBox':
      dir = Dir.FORWARD;
      pred = AutomationPredicate.comboBox;
      predErrorMsg = 'no_next_combo_box';
      break;
    case 'previousComboBox':
      dir = Dir.BACKWARD;
      pred = AutomationPredicate.comboBox;
      predErrorMsg = 'no_previous_combo_box';
      break;
    case 'nextEditText':
      dir = Dir.FORWARD;
      pred = AutomationPredicate.editText;
      predErrorMsg = 'no_next_edit_text';
      break;
    case 'previousEditText':
      dir = Dir.BACKWARD;
      pred = AutomationPredicate.editText;
      predErrorMsg = 'no_previous_edit_text';
      break;
    case 'nextFormField':
      dir = Dir.FORWARD;
      pred = AutomationPredicate.formField;
      predErrorMsg = 'no_next_form_field';
      break;
    case 'previousFormField':
      dir = Dir.BACKWARD;
      pred = AutomationPredicate.formField;
      predErrorMsg = 'no_previous_form_field';
      break;
    case 'nextHeading':
      dir = Dir.FORWARD;
      pred = AutomationPredicate.heading;
      predErrorMsg = 'no_next_heading';
      break;
    case 'nextHeading1':
      dir = Dir.FORWARD;
      pred = AutomationPredicate.makeHeadingPredicate(1);
      predErrorMsg = 'no_next_heading_1';
      break;
    case 'nextHeading2':
      dir = Dir.FORWARD;
      pred = AutomationPredicate.makeHeadingPredicate(2);
      predErrorMsg = 'no_next_heading_2';
      break;
    case 'nextHeading3':
      dir = Dir.FORWARD;
      pred = AutomationPredicate.makeHeadingPredicate(3);
      predErrorMsg = 'no_next_heading_3';
      break;
    case 'nextHeading4':
      dir = Dir.FORWARD;
      pred = AutomationPredicate.makeHeadingPredicate(4);
      predErrorMsg = 'no_next_heading_4';
      break;
    case 'nextHeading5':
      dir = Dir.FORWARD;
      pred = AutomationPredicate.makeHeadingPredicate(5);
      predErrorMsg = 'no_next_heading_5';
      break;
    case 'nextHeading6':
      dir = Dir.FORWARD;
      pred = AutomationPredicate.makeHeadingPredicate(6);
      predErrorMsg = 'no_next_heading_6';
      break;
    case 'previousHeading':
      dir = Dir.BACKWARD;
      pred = AutomationPredicate.heading;
      predErrorMsg = 'no_previous_heading';
      break;
    case 'previousHeading1':
      dir = Dir.BACKWARD;
      pred = AutomationPredicate.makeHeadingPredicate(1);
      predErrorMsg = 'no_previous_heading_1';
      break;
    case 'previousHeading2':
      dir = Dir.BACKWARD;
      pred = AutomationPredicate.makeHeadingPredicate(2);
      predErrorMsg = 'no_previous_heading_2';
      break;
    case 'previousHeading3':
      dir = Dir.BACKWARD;
      pred = AutomationPredicate.makeHeadingPredicate(3);
      predErrorMsg = 'no_previous_heading_3';
      break;
    case 'previousHeading4':
      dir = Dir.BACKWARD;
      pred = AutomationPredicate.makeHeadingPredicate(4);
      predErrorMsg = 'no_previous_heading_4';
      break;
    case 'previousHeading5':
      dir = Dir.BACKWARD;
      pred = AutomationPredicate.makeHeadingPredicate(5);
      predErrorMsg = 'no_previous_heading_5';
      break;
    case 'previousHeading6':
      dir = Dir.BACKWARD;
      pred = AutomationPredicate.makeHeadingPredicate(6);
      predErrorMsg = 'no_previous_heading_6';
      break;
    case 'nextLink':
      dir = Dir.FORWARD;
      pred = AutomationPredicate.link;
      predErrorMsg = 'no_next_link';
      break;
    case 'previousLink':
      dir = Dir.BACKWARD;
      pred = AutomationPredicate.link;
      predErrorMsg = 'no_previous_link';
      break;
    case 'nextTable':
      dir = Dir.FORWARD;
      pred = AutomationPredicate.table;
      predErrorMsg = 'no_next_table';
      break;
    case 'previousTable':
      dir = Dir.BACKWARD;
      pred = AutomationPredicate.table;
      predErrorMsg = 'no_previous_table';
      break;
    case 'nextVisitedLink':
      dir = Dir.FORWARD;
      pred = AutomationPredicate.visitedLink;
      predErrorMsg = 'no_next_visited_link';
      break;
    case 'previousVisitedLink':
      dir = Dir.BACKWARD;
      pred = AutomationPredicate.visitedLink;
      predErrorMsg = 'no_previous_visited_link';
      break;
    case 'right':
    case 'nextObject':
      current = current.move(cursors.Unit.NODE, Dir.FORWARD);
      break;
    case 'left':
    case 'previousObject':
      current = current.move(cursors.Unit.NODE, Dir.BACKWARD);
      break;
    case 'jumpToTop':
      var node = AutomationUtil.findNodePost(
          current.start.node.root, Dir.FORWARD, AutomationPredicate.leaf);
    if (node)
      current = cursors.Range.fromNode(node);
      break;
    case 'jumpToBottom':
      var node = AutomationUtil.findNodePost(
          current.start.node.root, Dir.BACKWARD, AutomationPredicate.leaf);
      if (node)
        current = cursors.Range.fromNode(node);
      break;
    case 'forceClickOnCurrentItem':
      if (ChromeVoxState.instance.currentRange_) {
        var actionNode = ChromeVoxState.instance.currentRange_.start.node;
        if (actionNode.role == RoleType.inlineTextBox)
          actionNode = actionNode.parent;
        actionNode.doDefault();
      }
      // Skip all other processing; if focus changes, we should get an event
      // for that.
      return false;
    case 'readFromHere':
      ChromeVoxState.isReadingContinuously = true;
      var continueReading = function() {
        if (!ChromeVoxState.isReadingContinuously ||
            !ChromeVoxState.instance.currentRange_)
          return;

        var prevRange = ChromeVoxState.instance.currentRange_;
        var newRange =
            ChromeVoxState.instance.currentRange_.move(
                cursors.Unit.NODE, Dir.FORWARD);

        // Stop if we've wrapped back to the document.
        var maybeDoc = newRange.start.node;
        if (maybeDoc.role == RoleType.rootWebArea &&
            maybeDoc.parent.root.role == RoleType.desktop) {
          ChromeVoxState.isReadingContinuously = false;
          return;
        }

        ChromeVoxState.instance.setCurrentRange(newRange);

        new Output()
            .withRichSpeechAndBraille(ChromeVoxState.instance.currentRange_,
                                      prevRange,
                                      Output.EventType.NAVIGATE)
            .onSpeechEnd(continueReading)
            .go();
      }.bind(this);

      new Output()
          .withRichSpeechAndBraille(ChromeVoxState.instance.currentRange_,
                                    null,
                                    Output.EventType.NAVIGATE)
          .onSpeechEnd(continueReading)
          .go();

      return false;
    case 'contextMenu':
      if (ChromeVoxState.instance.currentRange_) {
        var actionNode = ChromeVoxState.instance.currentRange_.start.node;
        if (actionNode.role == RoleType.inlineTextBox)
          actionNode = actionNode.parent;
        actionNode.showContextMenu();
        return false;
      }
      break;
    case 'toggleKeyboardHelp':
      ChromeVoxState.instance.startExcursion();
      (new PanelCommand(PanelCommandType.OPEN_MENUS)).send();
      return false;
    case 'showHeadingsList':
      ChromeVoxState.instance.startExcursion();
      (new PanelCommand(PanelCommandType.OPEN_MENUS, 'role_heading')).send();
      return false;
    case 'showFormsList':
      ChromeVoxState.instance.startExcursion();
      (new PanelCommand(PanelCommandType.OPEN_MENUS, 'role_form')).send();
      return false;
    case 'showLandmarksList':
      ChromeVoxState.instance.startExcursion();
      (new PanelCommand(PanelCommandType.OPEN_MENUS, 'role_landmark')).send();
      return false;
    case 'showLinksList':
      ChromeVoxState.instance.startExcursion();
      (new PanelCommand(PanelCommandType.OPEN_MENUS, 'role_link')).send();
      return false;
    case 'showTablesList':
      ChromeVoxState.instance.startExcursion();
      (new PanelCommand(PanelCommandType.OPEN_MENUS, 'table_strategy')).send();
      return false;
    case 'toggleSearchWidget':
      (new PanelCommand(PanelCommandType.SEARCH)).send();
      return false;
    case 'readCurrentTitle':
      var target = ChromeVoxState.instance.currentRange_.start.node;
      var output = new Output();

      if (target.root.role == RoleType.rootWebArea) {
        // Web.
        target = target.root;
        output.withString(target.name || target.docUrl);
      } else {
        // Views.
        while (target.role != RoleType.window) target = target.parent;
        if (target)
          output.withString(target.name || '');
      }
      output.go();
      return false;
    case 'readCurrentURL':
      var output = new Output();
      var target = ChromeVoxState.instance.currentRange_.start.node.root;
      output.withString(target.docUrl || '').go();
      return false;
    case 'copy':
      var textarea = document.createElement('textarea');
      document.body.appendChild(textarea);
      textarea.focus();
      document.execCommand('paste');
      var clipboardContent = textarea.value;
      textarea.remove();
      cvox.ChromeVox.tts.speak(
          Msgs.getMsg('copy', [clipboardContent]), cvox.QueueMode.FLUSH);
      ChromeVoxState.instance.pageSel_ = null;
      return true;
    case 'toggleSelection':
      if (!ChromeVoxState.instance.pageSel_) {
        ChromeVoxState.instance.pageSel_ = ChromeVoxState.instance.currentRange;
      } else {
        var root = ChromeVoxState.instance.currentRange_.start.node.root;
        if (root && root.anchorObject && root.focusObject) {
          var sel = new cursors.Range(
              new cursors.Cursor(root.anchorObject, root.anchorOffset),
              new cursors.Cursor(root.focusObject, root.focusOffset));
          var o = new Output()
                      .format('@end_selection')
                      .withSpeechAndBraille(sel, sel, Output.EventType.NAVIGATE)
                      .go();
        }
        ChromeVoxState.instance.pageSel_ = null;
        return false;
      }
      break;
    case 'fullyDescribe':
      var o = new Output();
      o.withContextFirst()
          .withRichSpeechAndBraille(current, null, Output.EventType.NAVIGATE)
          .go();
      return false;

    // Table commands.
    case 'previousRow':
      dir = Dir.BACKWARD;
      var tableOpts = {row: true, dir: dir};
      pred = AutomationPredicate.makeTableCellPredicate(
          current.start.node, tableOpts);
      predErrorMsg = 'no_cell_above';
      rootPred = AutomationPredicate.table;
      break;
    case 'previousCol':
      dir = Dir.BACKWARD;
      var tableOpts = {col: true, dir: dir};
      pred = AutomationPredicate.makeTableCellPredicate(
          current.start.node, tableOpts);
      predErrorMsg = 'no_cell_left';
      rootPred = AutomationPredicate.row;
      break;
    case 'nextRow':
      dir = Dir.FORWARD;
      var tableOpts = {row: true, dir: dir};
      pred = AutomationPredicate.makeTableCellPredicate(
          current.start.node, tableOpts);
      predErrorMsg = 'no_cell_below';
      rootPred = AutomationPredicate.table;
      break;
    case 'nextCol':
      dir = Dir.FORWARD;
      var tableOpts = {col: true, dir: dir};
      pred = AutomationPredicate.makeTableCellPredicate(
          current.start.node, tableOpts);
      predErrorMsg = 'no_cell_right';
      rootPred = AutomationPredicate.row;
      break;
    case 'goToRowFirstCell':
    case 'goToRowLastCell':
      var node = current.start.node;
      while (node && node.role != RoleType.row)
        node = node.parent;
      if (!node)
        break;
      var end = AutomationUtil.findNodePost(node,
          command == 'goToRowLastCell' ? Dir.BACKWARD : Dir.FORWARD,
          AutomationPredicate.leaf);
      if (end)
        current = cursors.Range.fromNode(end);
      break;
    case 'goToColFirstCell':
      dir = Dir.FORWARD;
      var node = current.start.node;
      while (node && node.role != RoleType.table)
        node = node.parent;
      if (!node || !node.firstChild)
        return false;
      var tableOpts = {col: true, dir: dir, end: true};
      pred = AutomationPredicate.makeTableCellPredicate(
          current.start.node, tableOpts);
      current = cursors.Range.fromNode(node.firstChild);
      // Should not be outputted.
      predErrorMsg = 'no_cell_above';
      rootPred = AutomationPredicate.table;
      break;
    case 'goToColLastCell':
      dir = Dir.BACKWARD;
      var node = current.start.node;
      while (node && node.role != RoleType.table)
        node = node.parent;
      if (!node || !node.lastChild)
        return false;
      var tableOpts = {col: true, dir: dir, end: true};
      pred = AutomationPredicate.makeTableCellPredicate(
          current.start.node, tableOpts);
      current = cursors.Range.fromNode(node.lastChild);
      // Should not be outputted.
      predErrorMsg = 'no_cell_below';
      rootPred = AutomationPredicate.table;
      break;
    case 'goToFirstCell':
    case 'goToLastCell':
      node = current.start.node;
      while (node && node.role != RoleType.table)
        node = node.parent;
      if (!node)
        break;
      var end = AutomationUtil.findNodePost(node,
          command == 'goToLastCell' ? Dir.BACKWARD : Dir.FORWARD,
          AutomationPredicate.leaf);
      if (end)
        current = cursors.Range.fromNode(end);
      break;
    default:
      return true;
  }

  if (pred) {
    var bound = current.getBound(dir).node;
    if (bound) {
      var node = AutomationUtil.findNextNode(
          bound, dir, pred, {skipInitialAncestry: true});

      if (node) {
        node = AutomationUtil.findNodePre(
                   node, Dir.FORWARD, AutomationPredicate.object) ||
            node;
      }

      if (node) {
        current = cursors.Range.fromNode(node);
      } else {
        if (predErrorMsg) {
          cvox.ChromeVox.tts.speak(
              Msgs.getMsg(predErrorMsg), cvox.QueueMode.FLUSH);
        }
        return false;
      }
    }
  }

  if (current)
    ChromeVoxState.instance.navigateToRange(current, undefined, speechProps);

  return false;
};

/**
 * React to mode changes.
 * @param {ChromeVoxMode} newMode
 * @param {ChromeVoxMode?} oldMode
 */
CommandHandler.onModeChanged = function(newMode, oldMode) {
  // Previously uninitialized.
  if (!oldMode)
    cvox.ChromeVoxKbHandler.commandHandler = CommandHandler.onCommand;

  var hasListener =
      chrome.commands.onCommand.hasListener(CommandHandler.onCommand);
  if (newMode == ChromeVoxMode.CLASSIC && hasListener)
    chrome.commands.onCommand.removeListener(CommandHandler.onCommand);
  else if (newMode == ChromeVoxMode.CLASSIC && !hasListener)
    chrome.commands.onCommand.addListener(CommandHandler.onCommand);
};

/**
 * Increase or decrease a speech property and make an announcement.
 * @param {string} propertyName The name of the property to change.
 * @param {boolean} increase If true, increases the property value by one
 *     step size, otherwise decreases.
 * @private
 */
CommandHandler.increaseOrDecreaseSpeechProperty_ =
    function(propertyName, increase) {
  cvox.ChromeVox.tts.increaseOrDecreaseProperty(propertyName, increase);
  var announcement;
  var valueAsPercent = Math.round(
      cvox.ChromeVox.tts.propertyToPercentage(propertyName) * 100);
  switch (propertyName) {
    case cvox.AbstractTts.RATE:
      announcement = Msgs.getMsg('announce_rate', [valueAsPercent]);
      break;
    case cvox.AbstractTts.PITCH:
      announcement = Msgs.getMsg('announce_pitch', [valueAsPercent]);
      break;
    case cvox.AbstractTts.VOLUME:
      announcement = Msgs.getMsg('announce_volume', [valueAsPercent]);
      break;
  }
  if (announcement) {
    cvox.ChromeVox.tts.speak(
        announcement, cvox.QueueMode.FLUSH,
        cvox.AbstractTts.PERSONALITY_ANNOTATION);
  }
};

}); //  goog.scope
