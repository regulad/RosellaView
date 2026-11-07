// A native UI component (not a turbomodule): the runtime loads
// xyz.regulad.rosellaview.surface.so and asks its IViewManagerFactory for the
// "RosellaSurface" view manager.
module.exports = {
  dependency: {
    platforms: {
      kepler: {
        autolink: {
          RosellaSurface: {
            uiComponentName: '/xyz.regulad.rosellaview.surface',
            linkDynamic: true,
            provider: 'application',
            components: ['RosellaSurface'],
            turbomodules: [],
          },
        },
      },
    },
  },
};
