// StreamConfig.cpp

#include "StreamConfig.h"

UStreamConfig::UStreamConfig()
{
	DashReminderChatLines = {
		NSLOCTEXT("StreamLearningReminders", "Dash01", "you know you have a dash, right?"),
		NSLOCTEXT("StreamLearningReminders", "Dash02", "that dash button is gathering dust"),
		NSLOCTEXT("StreamLearningReminders", "Dash03", "bro, use the dash")
	};

	AbilityReminderChatLines = {
		NSLOCTEXT("StreamLearningReminders", "Ability01", "that ability icon isn't decorative"),
		NSLOCTEXT("StreamLearningReminders", "Ability02", "you picked up an ability — use it!"),
		NSLOCTEXT("StreamLearningReminders", "Ability03", "press the ability button already")
	};

	ChargedPropExplosionReminderChatLines = {
		NSLOCTEXT("StreamLearningReminders", "PropExplosion01", "charge a prop and make it explode"),
		NSLOCTEXT("StreamLearningReminders", "PropExplosion02", "the props explode when you charge them enough"),
		NSLOCTEXT("StreamLearningReminders", "PropExplosion03", "full-charge a prop and send it!")
	};
}
