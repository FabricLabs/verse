module.exports = function (CHANCE_TABLE = {
  probabilities: [0.05],
  types: ['battle']
}) {
  return new Promise((resolve, reject) => {
    const luck = Math.random();
    const sum = CHANCE_TABLE.probabilities.reduce((a, b) => a + b);
    console.log('sum:', sum);
    if (luck > sum) resolve(null);

    const encounter = {
      type: CHANCE_TABLE.types[Math.floor(Math.random() * CHANCE_TABLE.types.length)]
    };

    resolve(encounter);
  });
};
