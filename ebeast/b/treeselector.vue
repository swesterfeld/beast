<!-- This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0 -->

<docs>
  # B-TREESELECTOR
  A [b-modaldialog] that displays a tree and allows selections.
  ## Events:
  *close*
  : A *close* event is emitted once the "Close" button activated.
</docs>

<style lang="scss">
  @import 'mixins.scss';
  .b-treeselector {
    margin: 0 $b-menu-hpad;
    .b-modaldialog-container	{ max-width: 70em; height: 90vh; align-items: stretch; }
    .b-modaldialog-body	{ flex-grow: 1; }
  }
</style>

<template>
  <div class="b-treeselector">
    <div slot="header">Tree Selector</div>
    <slot></slot>
    <ul class="b-treeselector-root"
	@keydown="focus_updown"
	v-if="tree.entries && tree.entries.length">
      <b-treeselector-item class=""
			    v-for="entry in tree.entries"
			    :entries="entry.entries"
			    :label="entry.label"
			    :uri="entry.uri"
			    :disabled="entry.disabled"
			    :key="entry.label + ';' + entry.uri" >
      </b-treeselector-item>
    </ul>
  </div>
</template>

<script>
// Example data
const tree_data = {
  label: 'ROOT ENTRY',
  entries: [
    { label: 'Hello-1' },
    { label: 'Second Choice' },
    {
      label: 'Expandable Children',
      entries: [
	{
	  label: 'Subfolder Stuff',
	  entries: [
	    { label: 'A - One' },
	    { label: 'B - Two' },
	    { label: 'C - Three' },
	  ]
	},
	{ label: 'Ying' },
	{ label: 'Yang' },
	{
	  label: 'More Things...',
	  entries: [
	    { label: 'Abcdefg' },
	    { label: 'Hijklmnop' },
	    { label: 'Qrstuvwxyz' },
	  ]
	}
      ]
    }
  ]
};

export default {
  name: 'b-treeselector',
  props: {
    tree: { default: () => Object.freeze (tree_data) },
  },
  data: function() {
    // create_data().then (r => this.tree = r);
    return {};
  },
  methods: {
    dummy (method, e) {
    },
    focus_updown (event) {
      const UP = 38;
      const DOWN = 40;
      if (event.keyCode == UP || event.keyCode == DOWN)
	{
	  const nodes = Util.list_focusables (this.$el); // selector for focussable elements
	  const array1 = [].slice.call (nodes);
	  // filter elements with display:none parents
	  const array = array1.filter (element => element.offsetWidth > 0 && element.offsetHeight > 0);
	  const idx = array.indexOf (document.activeElement);
	  const next = idx + (event.keyCode == UP ? -1 : +1);
	  if (next >= 0 && next < array.length)
	    {
	      event.preventDefault();
	      array[next].focus();
	      return true;
	    }
	}
      return Util.keydown_move_focus (event);
    },
  },
};
</script>
