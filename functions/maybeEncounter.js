module.exports = function (CHANCE_TABLE = {
  probabilities: [0.05],
  types: ['battle']
}) {
  return new Promise((resolve, reject) => {
    // How lucky are we?
    const luck = Math.random();
    const sum = CHANCE_TABLE.probabilities.reduce((a, b) => a + b);

    // Luck out
    if (luck > sum) resolve(null);

    // Select encounter type
    const encounter = {
      type: CHANCE_TABLE.types[Math.floor(luck * CHANCE_TABLE.types.length)]
    };

    // Serve the encounter
    resolve(encounter);
  });
};
