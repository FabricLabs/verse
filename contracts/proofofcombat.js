if (bot) clearInterval(bot);

const bot = setInterval(() => {
  const delay = $('.is-in-delay');
  const isReady = $('.no-delay');
  const healthEquation = $('#hero-stats-health')[0].innerHTML;
  console.log('health equation:', healthEquation);
  const healthValues = healthEquation.split('/').map((x) => {
    return x.replace(/\,/g, '');
  });

  const health = healthValues[0] / healthValues[1];

  console.log('health:', health);

  if (isReady) {
    if (health < 0.2) {
      $('#heal-button').click();
    } else {
      $('#attack-with-melee').click();
    }
  }
}, 25);
