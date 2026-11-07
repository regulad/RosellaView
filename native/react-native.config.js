module.exports = {
  dependency: {
    platforms: {
      kepler: {
        autolink: {
          RosellaView: {
            libraryName: 'libRosellaView.so',
            linkDynamic: true,
            provider: 'application',
            components: [],
            turbomodules: ['RosellaView'],
          },
        },
      },
    },
  },
};
