<!-- This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0 -->

<docs>
  # B-DEVICEPANEL
  Panel for editing of devices.
  ## Props:
  *track*
  : Container for the devices.
</docs>

<style lang="scss">
  @import 'mixins.scss';
  .b-devicepanel {
    border: 5px solid #755;
  }
</style>

<template>
  <b-hflex class="b-devicepanel" style="width: 100%; height: 100%" @contextmenu.stop="menuopen" >
    <b-deviceeditor v-for="device in devices" :key="device.$id"
		    :device="device" center style="margin: 5px" >
      {{ device.$id }}
    </b-deviceeditor>
    <b-contextmenu ref="cmenu" @click="menuactivation" >
      <b-menutitle> Device </b-menutitle>
      <b-menuitem fa="plus-circle"      role="add-device" >      Add Device		</b-menuitem>
      <b-menuitem fa="times-circle"     role="delete-device" >   Delete Device		</b-menuitem>
    </b-contextmenu>
  </b-hflex>
</template>

<script>
function observable_device_data () {
  const data = {
    devices:	{ default: [],  	notify: n => this.track.on ("notify:devices", n),
		  getter: async c => Object.freeze (await this.track.list_devices()), },
  };
  return this.observable_from_getters (data, () => this.track);
}

module.exports = {
  name: 'b-devicepanel',
  props: {
    track: { type: Bse.Track, },
  },
  data() { return observable_device_data.call (this); },
  methods: {
    menuactivation (role) {
      // close popup to remove focus guards
      this.$refs.cmenu.close();
      if (role == 'add-device')
	this.track.create_device ('Dummy');
    },
    menucheck (role, component) {
      console.log ("devicepanel.vue", this.track);
      if (!this.track)
	return false;
      switch (role)
      {
	case 'add-device':   return true;
      }
      return false;
    },
    menuopen (event) {
      this.$refs.cmenu.popup (event, { check: this.menucheck.bind (this) });
    },
  },
};
</script>